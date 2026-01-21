// Vivid Effects - GPU Particle Simulation Shader
// 3D particle simulation with forces (curl noise, gravity, attractors, vortex, wind)

// GPU particle data (64 bytes, cache-aligned)
struct Particle {
    posX: f32, posY: f32, posZ: f32,   // 12 bytes - position
    velX: f32, velY: f32, velZ: f32,   // 12 bytes - velocity (total: 24)
    life: f32, maxLife: f32,            // 8 bytes - lifetime (total: 32)
    size: f32, rotation: f32,           // 8 bytes - visual (total: 40)
    colorR: f32, colorG: f32, colorB: f32, colorA: f32,  // 16 bytes - color (total: 56)
    seed: f32, _pad: f32,               // 8 bytes (total: 64)
}

struct SimulateUniforms {
    dt: f32,
    time: f32,
    particleCount: u32,
    is3D: u32,  // 0 = 2D, 1 = 3D

    // Curl noise
    curlStrength: f32,
    curlScale: f32,
    curlSpeed: f32,
    curlOctaves: i32,

    // Gravity
    gravityX: f32,
    gravityY: f32,
    gravityZ: f32,
    drag: f32,

    // Turbulence
    turbulence: f32,
    turbulenceSeed: f32,
    _pad0: f32,
    _pad1: f32,

    // Attractor
    attractorX: f32,
    attractorY: f32,
    attractorZ: f32,
    attractorStrength: f32,

    // Vortex
    vortexCenterX: f32,
    vortexCenterY: f32,
    vortexCenterZ: f32,
    vortexStrength: f32,
    vortexAxisX: f32,
    vortexAxisY: f32,
    vortexAxisZ: f32,
    vortexFalloff: f32,

    // Wind
    windDirX: f32,
    windDirY: f32,
    windDirZ: f32,
    windStrength: f32,
    windGustStrength: f32,
    windGustFrequency: f32,
    aspectRatio: f32,
    _windPad1: f32,
}

@group(0) @binding(0) var<uniform> u: SimulateUniforms;
@group(0) @binding(1) var<storage, read> particlesIn: array<Particle>;
@group(0) @binding(2) var<storage, read_write> particlesOut: array<Particle>;

// =============================================================================
// 3D Simplex Noise
// =============================================================================

fn mod289_3(x: vec3f) -> vec3f { return x - floor(x * (1.0 / 289.0)) * 289.0; }
fn mod289_4(x: vec4f) -> vec4f { return x - floor(x * (1.0 / 289.0)) * 289.0; }
fn permute(x: vec4f) -> vec4f { return mod289_4(((x * 34.0) + 1.0) * x); }
fn taylorInvSqrt(r: vec4f) -> vec4f { return 1.79284291400159 - 0.85373472095314 * r; }

fn snoise(v: vec3f) -> f32 {
    let C = vec2f(1.0/6.0, 1.0/3.0);
    let D = vec4f(0.0, 0.5, 1.0, 2.0);

    var i = floor(v + dot(v, vec3f(C.y)));
    let x0 = v - i + dot(i, vec3f(C.x));

    let g = step(x0.yzx, x0.xyz);
    let l = 1.0 - g;
    let i1 = min(g.xyz, l.zxy);
    let i2 = max(g.xyz, l.zxy);

    let x1 = x0 - i1 + C.x;
    let x2 = x0 - i2 + C.y;
    let x3 = x0 - D.yyy;

    i = mod289_3(i);
    let p = permute(permute(permute(
        i.z + vec4f(0.0, i1.z, i2.z, 1.0))
        + i.y + vec4f(0.0, i1.y, i2.y, 1.0))
        + i.x + vec4f(0.0, i1.x, i2.x, 1.0));

    let n_ = 0.142857142857;
    let ns = n_ * D.wyz - D.xzx;

    let j = p - 49.0 * floor(p * ns.z * ns.z);

    let x_ = floor(j * ns.z);
    let y_ = floor(j - 7.0 * x_);

    let x = x_ * ns.x + ns.yyyy;
    let y = y_ * ns.x + ns.yyyy;
    let h = 1.0 - abs(x) - abs(y);

    let b0 = vec4f(x.xy, y.xy);
    let b1 = vec4f(x.zw, y.zw);

    let s0 = floor(b0) * 2.0 + 1.0;
    let s1 = floor(b1) * 2.0 + 1.0;
    let sh = -step(h, vec4f(0.0));

    let a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    let a1 = b1.xzyw + s1.xzyw * sh.zzww;

    var p0 = vec3f(a0.xy, h.x);
    var p1 = vec3f(a0.zw, h.y);
    var p2 = vec3f(a1.xy, h.z);
    var p3 = vec3f(a1.zw, h.w);

    let norm = taylorInvSqrt(vec4f(dot(p0,p0), dot(p1,p1), dot(p2,p2), dot(p3,p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    var m = max(0.6 - vec4f(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), vec4f(0.0));
    m = m * m;
    return 42.0 * dot(m*m, vec4f(dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3)));
}

// FBM (fractal brownian motion) noise
fn fbm(p: vec3f, octaves: i32) -> f32 {
    var result = 0.0;
    var amplitude = 1.0;
    var frequency = 1.0;
    var maxAmp = 0.0;

    for (var i = 0; i < octaves; i++) {
        result += amplitude * snoise(p * frequency);
        maxAmp += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return result / maxAmp;
}

// Extended FBM with configurable lacunarity and persistence
fn fbmEx(p: vec3f, octaves: i32, lacunarity: f32, persistence: f32) -> f32 {
    var result = 0.0;
    var amplitude = 1.0;
    var frequency = 1.0;
    var maxAmp = 0.0;

    for (var i = 0; i < octaves; i++) {
        result += amplitude * snoise(p * frequency);
        maxAmp += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return result / maxAmp;
}

// 3D Curl noise - divergence-free velocity field
fn curlNoise3D(pos: vec3f, z: f32, scale: f32, octaves: i32) -> vec3f {
    let eps = 0.001;
    let p = pos * scale;

    // Sample noise at offset positions for finite differences
    let n = vec3f(
        fbm(vec3f(p.x, p.y + 100.0, p.z + z), octaves),
        fbm(vec3f(p.x + 200.0, p.y, p.z + z + 100.0), octaves),
        fbm(vec3f(p.x + 100.0, p.y + 300.0, p.z + z + 200.0), octaves)
    );

    let nx = vec3f(
        fbm(vec3f(p.x + eps, p.y + 100.0, p.z + z), octaves),
        fbm(vec3f(p.x + eps + 200.0, p.y, p.z + z + 100.0), octaves),
        fbm(vec3f(p.x + eps + 100.0, p.y + 300.0, p.z + z + 200.0), octaves)
    );

    let ny = vec3f(
        fbm(vec3f(p.x, p.y + eps + 100.0, p.z + z), octaves),
        fbm(vec3f(p.x + 200.0, p.y + eps, p.z + z + 100.0), octaves),
        fbm(vec3f(p.x + 100.0, p.y + eps + 300.0, p.z + z + 200.0), octaves)
    );

    let nz = vec3f(
        fbm(vec3f(p.x, p.y + 100.0, p.z + eps + z), octaves),
        fbm(vec3f(p.x + 200.0, p.y, p.z + eps + z + 100.0), octaves),
        fbm(vec3f(p.x + 100.0, p.y + 300.0, p.z + eps + z + 200.0), octaves)
    );

    // Curl = nabla x F
    let d = 2.0 * eps;
    return vec3f(
        (ny.z - n.z) / d - (nz.y - n.y) / d,
        (nz.x - n.x) / d - (nx.z - n.z) / d,
        (nx.y - n.y) / d - (ny.x - n.x) / d
    );
}

// 2D Curl noise (for Screen2D mode)
fn curlNoise2D(pos: vec2f, z: f32, scale: f32, octaves: i32) -> vec2f {
    let eps = 0.001;
    let p = vec3f(pos * scale, z);

    let n = fbm(p, octaves);
    let nx = fbm(vec3f(p.x + eps, p.y, p.z), octaves);
    let ny = fbm(vec3f(p.x, p.y + eps, p.z), octaves);

    let d = 2.0 * eps;
    // Perpendicular to gradient (divergence-free)
    return vec2f((ny - n) / d, -(nx - n) / d);
}

// Extended 3D Curl noise with configurable epsilon, lacunarity, persistence
fn curlNoise3DEx(pos: vec3f, z: f32, scale: f32, octaves: i32, eps: f32, lac: f32, pers: f32) -> vec3f {
    let p = pos * scale;

    // Sample noise at offset positions for finite differences
    let n = vec3f(
        fbmEx(vec3f(p.x, p.y + 100.0, p.z + z), octaves, lac, pers),
        fbmEx(vec3f(p.x + 200.0, p.y, p.z + z + 100.0), octaves, lac, pers),
        fbmEx(vec3f(p.x + 100.0, p.y + 300.0, p.z + z + 200.0), octaves, lac, pers)
    );

    let nx = vec3f(
        fbmEx(vec3f(p.x + eps, p.y + 100.0, p.z + z), octaves, lac, pers),
        fbmEx(vec3f(p.x + eps + 200.0, p.y, p.z + z + 100.0), octaves, lac, pers),
        fbmEx(vec3f(p.x + eps + 100.0, p.y + 300.0, p.z + z + 200.0), octaves, lac, pers)
    );

    let ny = vec3f(
        fbmEx(vec3f(p.x, p.y + eps + 100.0, p.z + z), octaves, lac, pers),
        fbmEx(vec3f(p.x + 200.0, p.y + eps, p.z + z + 100.0), octaves, lac, pers),
        fbmEx(vec3f(p.x + 100.0, p.y + eps + 300.0, p.z + z + 200.0), octaves, lac, pers)
    );

    let nz = vec3f(
        fbmEx(vec3f(p.x, p.y + 100.0, p.z + eps + z), octaves, lac, pers),
        fbmEx(vec3f(p.x + 200.0, p.y, p.z + eps + z + 100.0), octaves, lac, pers),
        fbmEx(vec3f(p.x + 100.0, p.y + 300.0, p.z + eps + z + 200.0), octaves, lac, pers)
    );

    // Curl = nabla x F
    let d = 2.0 * eps;
    return vec3f(
        (ny.z - n.z) / d - (nz.y - n.y) / d,
        (nz.x - n.x) / d - (nx.z - n.z) / d,
        (nx.y - n.y) / d - (ny.x - n.x) / d
    );
}

// Extended 2D Curl noise with configurable epsilon, lacunarity, persistence
fn curlNoise2DEx(pos: vec2f, z: f32, scale: f32, octaves: i32, eps: f32, lac: f32, pers: f32) -> vec2f {
    let p = vec3f(pos * scale, z);

    let n = fbmEx(p, octaves, lac, pers);
    let nx = fbmEx(vec3f(p.x + eps, p.y, p.z), octaves, lac, pers);
    let ny = fbmEx(vec3f(p.x, p.y + eps, p.z), octaves, lac, pers);

    let d = 2.0 * eps;
    // Perpendicular to gradient (divergence-free)
    return vec2f((ny - n) / d, -(nx - n) / d);
}

// Simple hash for turbulence
fn hash31(p: vec3f) -> f32 {
    var p3 = fract(p * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// =============================================================================
// Main Compute Kernel
// =============================================================================

@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let idx = id.x;
    if (idx >= u.particleCount) { return; }

    var p = particlesIn[idx];

    // Skip dead particles
    if (p.life <= 0.0) {
        particlesOut[idx] = p;
        return;
    }

    let is3D = u.is3D == 1u;
    var pos = vec3f(p.posX, p.posY, p.posZ);
    var vel = vec3f(p.velX, p.velY, p.velZ);

    // === Apply Forces ===

    // Curl noise
    if (u.curlStrength > 0.001) {
        let curlTime = u.time * u.curlSpeed + p.seed * 10.0;
        if (is3D) {
            let curl = curlNoise3D(pos, curlTime, u.curlScale, u.curlOctaves);
            vel += curl * u.curlStrength * u.dt;
        } else {
            let curl2D = curlNoise2D(pos.xy, curlTime, u.curlScale, u.curlOctaves);
            vel.x += curl2D.x * u.curlStrength * u.dt;
            vel.y += curl2D.y * u.curlStrength * u.dt;
        }
    }

    // Gravity
    vel += vec3f(u.gravityX, u.gravityY, u.gravityZ) * u.dt;

    // Drag
    if (u.drag > 0.001) {
        vel *= 1.0 - u.drag * u.dt;
    }

    // Turbulence (random jitter)
    if (u.turbulence > 0.001) {
        let seed = vec3f(p.seed * 1000.0, u.time * 100.0, f32(idx));
        let jitter = vec3f(
            hash31(seed) * 2.0 - 1.0,
            hash31(seed + vec3f(100.0, 0.0, 0.0)) * 2.0 - 1.0,
            hash31(seed + vec3f(0.0, 100.0, 0.0)) * 2.0 - 1.0
        );
        if (is3D) {
            vel += jitter * u.turbulence * u.dt;
        } else {
            vel.x += jitter.x * u.turbulence * u.dt;
            vel.y += jitter.y * u.turbulence * u.dt;
        }
    }

    // Attractor
    if (abs(u.attractorStrength) > 0.001) {
        let attPos = vec3f(u.attractorX, u.attractorY, u.attractorZ);
        let toAtt = attPos - pos;
        let dist = length(toAtt);
        if (dist > 0.01) {
            vel += normalize(toAtt) * u.attractorStrength * u.dt / dist;
        }
    }

    // Vortex
    if (abs(u.vortexStrength) > 0.001) {
        let vCenter = vec3f(u.vortexCenterX, u.vortexCenterY, u.vortexCenterZ);
        let vAxis = normalize(vec3f(u.vortexAxisX, u.vortexAxisY, u.vortexAxisZ));
        let toP = pos - vCenter;
        let alongAxis = dot(toP, vAxis);
        let radial = toP - vAxis * alongAxis;
        let dist = length(radial);
        if (dist > 0.01) {
            let tangent = cross(vAxis, normalize(radial));
            var forceMag = u.vortexStrength;
            if (u.vortexFalloff > 0.001) {
                forceMag /= pow(dist, u.vortexFalloff);
            }
            vel += tangent * forceMag * u.dt;
        }
    }

    // Wind
    if (abs(u.windStrength) > 0.001) {
        let windDir = normalize(vec3f(u.windDirX, u.windDirY, u.windDirZ));
        var windForce = u.windStrength;
        if (u.windGustStrength > 0.001) {
            let noiseInput = pos.x * u.windGustFrequency + pos.y * u.windGustFrequency + u.time * u.windGustFrequency;
            let gustValue = sin(noiseInput * 6.28318) * 0.5 + 0.5;
            windForce *= (1.0 + gustValue * u.windGustStrength);
        }
        vel += windDir * windForce * u.dt;
    }

    // Apply aspect ratio correction for 2D mode (keeps motion circular on widescreen)
    if (!is3D && u.aspectRatio > 0.001) {
        // Scale X velocity by 1/aspectRatio to compensate for wider screen space
        let originalVelX = vec3f(p.velX, p.velY, p.velZ).x;
        let velChangeX = vel.x - originalVelX;
        vel.x = originalVelX + velChangeX / u.aspectRatio;
    }

    // === Integrate Position ===
    pos += vel * u.dt;

    // Update rotation
    p.rotation += 0.5 * u.dt;  // Simple spin

    // Update life
    p.life -= u.dt;

    // Write back
    p.posX = pos.x;
    p.posY = pos.y;
    p.posZ = pos.z;
    p.velX = vel.x;
    p.velY = vel.y;
    p.velZ = vel.z;

    particlesOut[idx] = p;
}
