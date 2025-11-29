// Image Over Gradient Shader
// Generates animated gradient background and composites displaced image over it
// Uses uniforms:
//   u.param0 = displacement amount
//   u.param1 = noise scale
//   u.param2 = noise speed
//   u.param3 = gradient speed (for animated gradient)

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

// Generate animated gradient background
fn generateGradient(uv: vec2f, time: f32) -> vec3f {
    // Animated radial gradient with shifting colors
    let center = vec2f(0.5 + sin(time * 0.3) * 0.2, 0.5 + cos(time * 0.4) * 0.2);
    let dist = length(uv - center);

    // Two animated colors
    let hue1 = fract(time * 0.1);
    let hue2 = fract(time * 0.1 + 0.5);

    // Simple HSV to RGB (hue only, full saturation, full value)
    let color1 = hsvToRgb(vec3f(hue1, 0.7, 0.8));
    let color2 = hsvToRgb(vec3f(hue2, 0.6, 0.4));

    return mix(color1, color2, smoothstep(0.0, 0.8, dist));
}

// HSV to RGB conversion
fn hsvToRgb(hsv: vec3f) -> vec3f {
    let h = hsv.x * 6.0;
    let s = hsv.y;
    let v = hsv.z;

    let i = floor(h);
    let f = h - i;
    let p = v * (1.0 - s);
    let q = v * (1.0 - s * f);
    let t = v * (1.0 - s * (1.0 - f));

    let ii = i32(i) % 6;

    switch (ii) {
        case 0: { return vec3f(v, t, p); }
        case 1: { return vec3f(q, v, p); }
        case 2: { return vec3f(p, v, t); }
        case 3: { return vec3f(p, q, v); }
        case 4: { return vec3f(t, p, v); }
        default: { return vec3f(v, p, q); }
    }
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let amount = u.param0;
    let noiseScale = u.param1;
    let noiseSpeed = u.param2;
    let gradientSpeed = u.param3;

    // Generate animated gradient background
    let bg = generateGradient(in.uv, u.time * gradientSpeed);

    // Calculate noise displacement for foreground image
    let animTime = u.time * noiseSpeed;
    let noiseCoord = vec3f(in.uv * noiseScale, animTime);
    let noiseX = snoise(noiseCoord);
    let noiseY = snoise(noiseCoord + vec3f(100.0, 100.0, 0.0));

    let displacement = vec2f(noiseX, noiseY) * amount;
    let displacedUV = in.uv + displacement;

    // Sample image with displaced UVs (clamp to avoid wrapping)
    let clampedUV = clamp(displacedUV, vec2f(0.0), vec2f(1.0));
    let fg = textureSample(inputTexture, inputSampler, clampedUV);

    // Fade edges where UV would sample outside
    let edgeFade = smoothstep(0.0, 0.02, min(
        min(displacedUV.x, 1.0 - displacedUV.x),
        min(displacedUV.y, 1.0 - displacedUV.y)
    ));
    let fgAlpha = fg.a * edgeFade;

    // Alpha-over compositing: fg over bg
    let outRgb = fg.rgb * fgAlpha + bg * (1.0 - fgAlpha);

    return vec4f(outRgb, 1.0);
}
