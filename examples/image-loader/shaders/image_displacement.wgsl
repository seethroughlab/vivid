// Image Displacement Shader
// Applies animated simplex noise displacement to an image
// Preserves alpha channel for transparent images
// Uses uniforms:
//   u.param0 = displacement amount
//   u.param1 = noise scale
//   u.param2 = noise speed

// Simplex noise helpers
fn mod289_3(x: vec3f) -> vec3f { return x - floor(x * (1.0 / 289.0)) * 289.0; }
fn mod289_4(x: vec4f) -> vec4f { return x - floor(x * (1.0 / 289.0)) * 289.0; }
fn permute(x: vec4f) -> vec4f { return mod289_4(((x * 34.0) + 1.0) * x); }
fn taylorInvSqrt(r: vec4f) -> vec4f { return 1.79284291400159 - 0.85373472095314 * r; }

fn snoise(v: vec3f) -> f32 {
    let C = vec2f(1.0/6.0, 1.0/3.0);
    let D = vec4f(0.0, 0.5, 1.0, 2.0);

    var i = floor(v + dot(v, vec3f(C.y, C.y, C.y)));
    let x0 = v - i + dot(i, vec3f(C.x, C.x, C.x));

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

    let norm = taylorInvSqrt(vec4f(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    var m = max(0.6 - vec4f(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), vec4f(0.0));
    m = m * m;
    return 42.0 * dot(m * m, vec4f(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let amount = u.param0;
    let noiseScale = u.param1;
    let noiseSpeed = u.param2;

    // Animated time for flowing effect
    let animTime = u.time * noiseSpeed;

    // Sample noise at two different frequencies for X and Y displacement
    let noiseCoord = vec3f(in.uv * noiseScale, animTime);
    let noiseX = snoise(noiseCoord);
    let noiseY = snoise(noiseCoord + vec3f(100.0, 100.0, 0.0));

    // Apply displacement to UV coordinates
    let displacement = vec2f(noiseX, noiseY) * amount;
    let displacedUV = in.uv + displacement;

    // Sample image with displaced UVs
    // Use clamp to avoid wrapping artifacts at edges
    let clampedUV = clamp(displacedUV, vec2f(0.0), vec2f(1.0));
    let color = textureSample(inputTexture, inputSampler, clampedUV);

    // Preserve alpha - fade out at edges where UV was clamped
    var alpha = color.a;

    // Optional: Fade edges where displacement would sample outside
    let edgeFade = smoothstep(0.0, 0.02, min(
        min(displacedUV.x, 1.0 - displacedUV.x),
        min(displacedUV.y, 1.0 - displacedUV.y)
    ));
    alpha *= edgeFade;

    return vec4f(color.rgb, alpha);
}
