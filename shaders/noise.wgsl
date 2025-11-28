// Noise shader with configurable parameters
// Uses uniforms:
//   u.param0 = scale
//   u.param1 = phase (animated offset)
//   u.param2 = octaves
//   u.param3 = lacunarity
//   u.param4 = persistence

// Permutation polynomial for simplex noise
fn permute3(x: vec3f) -> vec3f {
    return (((x * 34.0) + 1.0) * x) % 289.0;
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

    i = i % 289.0;
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

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Read parameters from uniforms
    let scale = u.param0;
    let phase = u.param1;
    let octaves = i32(u.param2);
    let lacunarity = u.param3;
    let persistence = u.param4;

    let uv = in.uv * scale;
    let offset = vec2f(phase * 0.5, phase * 0.3);

    let n = fbm(uv + offset, octaves, lacunarity, persistence);
    let value = n * 0.5 + 0.5;  // Normalize to 0-1

    return vec4f(value, value, value, 1.0);
}
