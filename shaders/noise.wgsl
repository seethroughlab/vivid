// Noise shader with 4D simplex noise for smooth evolution
// Uses uniforms:
//   u.param0 = scale
//   u.param1 = phase (time)
//   u.param2 = octaves
//   u.param3 = lacunarity
//   u.param4 = persistence

fn mod289(x: vec4f) -> vec4f {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

fn permute(x: vec4f) -> vec4f {
    return mod289(((x * 34.0) + 1.0) * x);
}

fn taylorInvSqrt(r: vec4f) -> vec4f {
    return 1.79284291400159 - 0.85373472095314 * r;
}

// 4D Simplex Noise (WGSL compliant - no swizzle assignments)
fn snoise4(v: vec4f) -> f32 {
    let C = vec4f(
        0.138196601125011,   // (5 - sqrt(5))/20 = G4
        0.276393202250021,   // 2 * G4
        0.414589803375032,   // 3 * G4
        -0.447213595499958   // -1 + 4 * G4
    );
    let F4 = 0.309016994374947451;  // (sqrt(5) - 1) / 4

    // First corner
    var i = floor(v + dot(v, vec4f(F4)));
    let x0 = v - i + dot(i, vec4f(C.x));

    // Determine which simplex we're in
    var i0: vec4f;
    let isX = step(x0.yzw, vec3f(x0.x));
    let isYZ = step(x0.zww, vec3f(x0.y, x0.y, x0.z));

    i0 = vec4f(isX.x + isX.y + isX.z, 1.0 - isX.x, 1.0 - isX.y, 1.0 - isX.z);
    i0 = vec4f(i0.x, i0.y + isYZ.x + isYZ.y, i0.z + 1.0 - isYZ.x, i0.w + 1.0 - isYZ.y);
    i0 = vec4f(i0.x, i0.y, i0.z + isYZ.z, i0.w + 1.0 - isYZ.z);

    let i3 = clamp(i0, vec4f(0.0), vec4f(1.0));
    let i2 = clamp(i0 - 1.0, vec4f(0.0), vec4f(1.0));
    let i1 = clamp(i0 - 2.0, vec4f(0.0), vec4f(1.0));

    let x1 = x0 - i1 + vec4f(C.x);
    let x2 = x0 - i2 + vec4f(C.y);
    let x3 = x0 - i3 + vec4f(C.z);
    let x4 = x0 + vec4f(C.w);

    // Permutations
    i = mod289(i);
    let j0 = permute(permute(permute(permute(
        vec4f(i.w)) + vec4f(i.z)) + vec4f(i.y)) + vec4f(i.x));

    let j1 = permute(permute(permute(permute(
        i.w + vec4f(i1.w, i2.w, i3.w, 1.0))
        + i.z + vec4f(i1.z, i2.z, i3.z, 1.0))
        + i.y + vec4f(i1.y, i2.y, i3.y, 1.0))
        + i.x + vec4f(i1.x, i2.x, i3.x, 1.0));

    // Gradients: 7x7x6 points over a cube mapped to a 4-simplex
    let ip = vec4f(1.0/294.0, 1.0/49.0, 1.0/7.0, 0.0);

    let p0 = grad4(j0.x, ip);
    let p1 = grad4(j1.x, ip);
    let p2 = grad4(j1.y, ip);
    let p3 = grad4(j1.z, ip);
    let p4 = grad4(j1.w, ip);

    // Normalise gradients
    let norm0 = taylorInvSqrt(vec4f(dot(p0,p0), dot(p1,p1), dot(p2,p2), dot(p3,p3)));
    let np0 = p0 * norm0.x;
    let np1 = p1 * norm0.y;
    let np2 = p2 * norm0.z;
    let np3 = p3 * norm0.w;
    let np4 = p4 * taylorInvSqrt(vec4f(dot(p4,p4))).x;

    // Mix contributions from corners
    var m0 = max(0.6 - vec3f(dot(x0,x0), dot(x1,x1), dot(x2,x2)), vec3f(0.0));
    var m1 = max(0.6 - vec2f(dot(x3,x3), dot(x4,x4)), vec2f(0.0));
    m0 = m0 * m0;
    m1 = m1 * m1;

    return 49.0 * (
        dot(m0 * m0, vec3f(dot(np0, x0), dot(np1, x1), dot(np2, x2)))
        + dot(m1 * m1, vec2f(dot(np3, x3), dot(np4, x4)))
    );
}

fn grad4(j: f32, ip: vec4f) -> vec4f {
    let ones = vec4f(1.0, 1.0, 1.0, -1.0);

    let pxyz = floor(fract(vec3f(j) * ip.xyz) * 7.0) * ip.z - 1.0;
    let pw = 1.5 - dot(abs(pxyz), ones.xyz);

    let s = vec4f(
        select(0.0, 1.0, pxyz.x < 0.0),
        select(0.0, 1.0, pxyz.y < 0.0),
        select(0.0, 1.0, pxyz.z < 0.0),
        0.0
    );

    let adj = (s.xyz * 2.0 - 1.0) * select(0.0, 1.0, pw < 0.0);

    return vec4f(pxyz.x + adj.x, pxyz.y + adj.y, pxyz.z + adj.z, pw);
}

// FBM using 4D noise with time as circular path through 4D
fn fbm4(xy: vec2f, time: f32, octaves: i32, lacunarity: f32, persistence: f32) -> f32 {
    var value = 0.0;
    var amplitude = 0.5;
    var frequency = 1.0;

    // Circular path in z/w plane - this creates seamless looping evolution
    let radius = 1.0;
    let z = cos(time) * radius;
    let w = sin(time) * radius;

    for (var i = 0; i < octaves; i++) {
        // Scale all 4 dimensions by frequency for proper 4D FBM
        let p = vec4f(xy.x * frequency, xy.y * frequency, z * frequency, w * frequency);
        value += amplitude * snoise4(p);
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return value;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let scale = u.param0;
    let phase = u.param1;
    let octaves = i32(u.param2);
    let lacunarity = u.param3;
    let persistence = u.param4;

    let uv = in.uv * scale;

    // 4D noise with time as circular motion in z/w - evolves smoothly in place
    let n = fbm4(uv, phase, octaves, lacunarity, persistence);
    let value = n * 0.5 + 0.5;

    return vec4f(value, value, value, 1.0);
}
