// HSV Adjustment shader
// Uses uniforms:
//   u.param0 = hueShift (-1 to 1, wraps)
//   u.param1 = saturation multiplier
//   u.param2 = value multiplier

fn rgb2hsv(c: vec3f) -> vec3f {
    let K = vec4f(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    let p = mix(vec4f(c.bg, K.wz), vec4f(c.gb, K.xy), step(c.b, c.g));
    let q = mix(vec4f(p.xyw, c.r), vec4f(c.r, p.yzx), step(p.x, c.r));

    let d = q.x - min(q.w, q.y);
    let e = 1.0e-10;
    return vec3f(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

fn hsv2rgb(c: vec3f) -> vec3f {
    let K = vec4f(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    let p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, vec3f(0.0), vec3f(1.0)), c.y);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let hueShift = u.param0;
    let saturation = u.param1;
    let value = u.param2;

    let color = textureSample(inputTexture, inputSampler, in.uv).rgb;

    var hsv = rgb2hsv(color);
    hsv.x = fract(hsv.x + hueShift);
    hsv.y = hsv.y * saturation;
    hsv.z = hsv.z * value;

    let rgb = hsv2rgb(hsv);
    return vec4f(clamp(rgb, vec3f(0.0), vec3f(1.0)), 1.0);
}
