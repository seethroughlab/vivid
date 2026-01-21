// GPU Particles - Compute Shader for Particle Simulation
// Curl noise-based particle system with vortex and gravity forces

// @include "lib/noise.wgsl"

struct Particle {
    posX: f32, posY: f32,
    velX: f32, velY: f32,
    life: f32, maxLife: f32,
    size: f32, rotation: f32,
    colorR: f32, colorG: f32, colorB: f32, colorA: f32,
    seed: f32,
    _pad0: f32, _pad1: f32, _pad2: f32,
}

struct SimulateUniforms {
    dt: f32,
    time: f32,
    particleCount: u32,
    _pad0: f32,

    // Curl noise
    curlStrength: f32,
    curlScale: f32,
    curlSpeed: f32,
    curlOctaves: i32,

    // Vortex
    vortexStrength: f32,
    vortexCenterX: f32,
    vortexCenterY: f32,
    vortexFalloff: f32,

    // Gravity and drag
    gravityX: f32,
    gravityY: f32,
    drag: f32,
    _pad1: f32,
}

@group(0) @binding(0) var<uniform> u: SimulateUniforms;
@group(0) @binding(1) var<storage, read> particlesIn: array<Particle>;
@group(0) @binding(2) var<storage, read_write> particlesOut: array<Particle>;

// Curl noise for particles: perpendicular to gradient of 3D noise field
fn curlNoiseParticle(p: vec2f, z: f32, scale: f32) -> vec2f {
    let eps = 0.001;
    let sp = p * scale;

    // Compute partial derivatives via finite differences
    let n = snoise3(vec3f(sp, z));
    let nx = snoise3(vec3f(sp.x + eps, sp.y, z));
    let ny = snoise3(vec3f(sp.x, sp.y + eps, z));

    let dnx = (nx - n) / eps;
    let dny = (ny - n) / eps;

    // Curl in 2D: perpendicular to gradient (divergence-free)
    return vec2f(dny, -dnx);
}

fn curlNoiseFBMParticle(p: vec2f, z: f32, scale: f32, octaves: i32) -> vec2f {
    var result = vec2f(0.0);
    var amplitude = 1.0;
    var frequency = 1.0;
    var maxAmp = 0.0;

    for (var i = 0; i < octaves; i++) {
        result += amplitude * curlNoiseParticle(p, z, scale * frequency);
        maxAmp += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return result / maxAmp;
}

// Vortex force: tangential rotation around a center point
fn vortexForce(pos: vec2f, center: vec2f, strength: f32, falloff: f32) -> vec2f {
    let toCenter = center - pos;
    let dist = length(toCenter);
    if (dist < 0.0001) { return vec2f(0.0); }

    let tangent = vec2f(-toCenter.y, toCenter.x) / dist;
    let attenuation = exp(-dist / falloff);

    return tangent * strength * attenuation;
}

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

    // Current state
    let pos = vec2f(p.posX, p.posY);
    var vel = vec2f(p.velX, p.velY);

    // === Apply Forces ===

    // Curl noise (primary force field for organic motion)
    if (u.curlStrength > 0.001) {
        let curlZ = u.time * u.curlSpeed + p.seed * 10.0;
        let curl = curlNoiseFBMParticle(pos, curlZ, u.curlScale, u.curlOctaves);
        vel += curl * u.curlStrength * u.dt;
    }

    // Vortex rotation
    if (abs(u.vortexStrength) > 0.001) {
        let vortex = vortexForce(pos, vec2f(u.vortexCenterX, u.vortexCenterY),
                                  u.vortexStrength, u.vortexFalloff);
        vel += vortex * u.dt;
    }

    // Gravity
    vel += vec2f(u.gravityX, u.gravityY) * u.dt;

    // Drag
    if (u.drag > 0.001) {
        vel *= 1.0 - u.drag * u.dt;
    }

    // === Integrate Position ===
    let newPos = pos + vel * u.dt;

    p.posX = newPos.x;
    p.posY = newPos.y;
    p.velX = vel.x;
    p.velY = vel.y;

    // Update life
    p.life -= u.dt;

    particlesOut[idx] = p;
}
