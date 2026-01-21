// Vivid Core - Noise Functions Library
// Simplex noise, curl noise, and related utilities

// ============================================================================
// Helper Functions
// ============================================================================

fn mod289_3(x: vec3f) -> vec3f {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

fn mod289_4(x: vec4f) -> vec4f {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

fn permute(x: vec4f) -> vec4f {
    return mod289_4(((x * 34.0) + 1.0) * x);
}

fn taylorInvSqrt(r: vec4f) -> vec4f {
    return 1.79284291400159 - 0.85373472095314 * r;
}

// ============================================================================
// 3D Simplex Noise
// ============================================================================

fn snoise3(v: vec3f) -> f32 {
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

// ============================================================================
// 2D Simplex Noise (derived from 3D with z=0)
// ============================================================================

fn snoise2(v: vec2f) -> f32 {
    return snoise3(vec3f(v, 0.0));
}

// ============================================================================
// Curl Noise (divergence-free noise for fluid-like motion)
// ============================================================================

// 2D curl noise using 3D simplex noise
fn curlNoise2d(p: vec2f, z: f32, scale: f32) -> vec2f {
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

// Curl noise with FBM (Fractal Brownian Motion)
fn curlNoiseFbm(p: vec2f, z: f32, scale: f32, octaves: i32) -> vec2f {
    var result = vec2f(0.0);
    var amplitude = 1.0;
    var frequency = 1.0;
    var maxAmp = 0.0;

    for (var i = 0; i < octaves; i++) {
        result += amplitude * curlNoise2d(p, z, scale * frequency);
        maxAmp += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return result / maxAmp;
}

// ============================================================================
// FBM (Fractal Brownian Motion)
// ============================================================================

fn fbm2(p: vec2f, octaves: i32) -> f32 {
    var result = 0.0;
    var amplitude = 0.5;
    var frequency = 1.0;
    var pos = p;

    for (var i = 0; i < octaves; i++) {
        result += amplitude * snoise2(pos * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return result;
}

fn fbm3(p: vec3f, octaves: i32) -> f32 {
    var result = 0.0;
    var amplitude = 0.5;
    var frequency = 1.0;
    var pos = p;

    for (var i = 0; i < octaves; i++) {
        result += amplitude * snoise3(pos * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return result;
}

// ============================================================================
// Simple Hash Functions (for quick random values)
// ============================================================================

fn hash11(p: f32) -> f32 {
    var p3 = fract(p * 0.1031);
    p3 += dot(p3, p3 + 33.33);
    return fract((p3 + p3) * p3);
}

fn hash21(p: vec2f) -> f32 {
    var p3 = fract(vec3f(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

fn hash22(p: vec2f) -> vec2f {
    var p3 = fract(vec3f(p.xyx) * vec3f(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}
