// Noise shader with configurable parameters
// Uses uniforms:
//   u.param0 = scale
//   u.param1 = phase (animated offset)
//   u.param2 = octaves
//   u.param3 = lacunarity
//   u.param4 = persistence

// Cross-platform modulo that avoids precision issues on D3D12
// Using x - y * floor(x / y) instead of x % y for consistency
fn mod289(x: vec3f) -> vec3f {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

fn mod289_2(x: vec2f) -> vec2f {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

// Permutation polynomial for simplex noise
fn permute3(x: vec3f) -> vec3f {
    return mod289(((x * 34.0) + 1.0) * x);
}

// Simplex 2D noise
fn snoise2(v: vec2f) -> f32 {
    let C = vec4f(0.211324865405187, 0.366025403784439,
                  -0.577350269189626, 0.024390243902439);

    var i = floor(v + dot(v, C.yy));
    let x0 = v - i + dot(i, C.xx);

    var i1: vec2f;
    if (x0.x > x0.y) {
        i1 = vec2f(1.0, 0.0);
    } else {
        i1 = vec2f(0.0, 1.0);
    }

    var x12 = x0.xyxy + C.xxzz;
    x12 = vec4f(x12.xy - i1, x12.zw);

    i = mod289_2(i);
    let p = permute3(permute3(i.y + vec3f(0.0, i1.y, 1.0)) + i.x + vec3f(0.0, i1.x, 1.0));

    var m = max(0.5 - vec3f(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), vec3f(0.0));
    m = m * m;
    m = m * m;

    let x = 2.0 * fract(p * C.www) - 1.0;
    let h = abs(x) - 0.5;
    let ox = floor(x + 0.5);
    let a0 = x - ox;

    m = m * (1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h));

    let g = vec3f(
        a0.x * x0.x + h.x * x0.y,
        a0.y * x12.x + h.y * x12.y,
        a0.z * x12.z + h.z * x12.w
    );

    return 130.0 * dot(m, g);
}

// Fractal Brownian Motion with configurable octaves
fn fbm(p: vec2f, octaves: i32, lacunarity: f32, persistence: f32) -> f32 {
    var value = 0.0;
    var amplitude = 0.5;
    var frequency = 1.0;
    var pos = p;

    for (var i = 0; i < octaves; i++) {
        value += amplitude * snoise2(pos * frequency);
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return value;
}

fn mod289_3(x: vec3f) -> vec3f {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

fn mod289_4(x: vec4f) -> vec4f {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

fn permute4(x: vec4f) -> vec4f {
    return mod289_4(((x * 34.0) + 1.0) * x);
}

fn taylorInvSqrt4(r: vec4f) -> vec4f {
    return 1.79284291400159 - 0.85373472095314 * r;
}

// Proper 3D simplex noise
fn snoise3(v: vec3f) -> f32 {
    let C = vec2f(1.0 / 6.0, 1.0 / 3.0);
    let D = vec4f(0.0, 0.5, 1.0, 2.0);

    // First corner
    var i = floor(v + dot(v, C.yyy));
    let x0 = v - i + dot(i, C.xxx);

    // Other corners
    let g = step(x0.yzx, x0.xyz);
    let l = 1.0 - g;
    let i1 = min(g.xyz, l.zxy);
    let i2 = max(g.xyz, l.zxy);

    let x1 = x0 - i1 + C.xxx;
    let x2 = x0 - i2 + C.yyy;
    let x3 = x0 - D.yyy;

    // Permutations
    i = mod289_3(i);
    let p = permute4(permute4(permute4(
        i.z + vec4f(0.0, i1.z, i2.z, 1.0))
      + i.y + vec4f(0.0, i1.y, i2.y, 1.0))
      + i.x + vec4f(0.0, i1.x, i2.x, 1.0));

    // Gradients
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

    // Normalise gradients
    let norm = taylorInvSqrt4(vec4f(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    // Mix final noise value
    var m = max(0.6 - vec4f(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), vec4f(0.0));
    m = m * m;
    return 42.0 * dot(m * m, vec4f(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

// FBM with 3D noise for evolving animation
fn fbm3(p: vec3f, octaves: i32, lacunarity: f32, persistence: f32) -> f32 {
    var value = 0.0;
    var amplitude = 0.5;
    var frequency = 1.0;
    var pos = p;

    for (var i = 0; i < octaves; i++) {
        value += amplitude * snoise3(pos * frequency);
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return value;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Read parameters from uniforms
    let scale = u.param0;
    let phase = u.param1;
    let octaves = i32(u.param2);
    let lacunarity = u.param3;
    let persistence = u.param4;

    let uv = in.uv * scale;

    // Use 3D noise with time as Z - evolves in place instead of scrolling
    let n = fbm3(vec3f(uv, phase), octaves, lacunarity, persistence);
    let value = n * 0.5 + 0.5;  // Normalize to 0-1

    return vec4f(value, value, value, 1.0);
}
