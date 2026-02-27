fn hsv2rgb(c: vec3f) -> vec3f {
    let h = c.x * 6.0;
    let s = c.y;
    let v = c.z;

    let i = floor(h);
    let f = h - i;
    let p = v * (1.0 - s);
    let q = v * (1.0 - s * f);
    let t = v * (1.0 - s * (1.0 - f));

    let sector = i32(i) % 6;
    if (sector == 0) { return vec3f(v, t, p); }
    if (sector == 1) { return vec3f(q, v, p); }
    if (sector == 2) { return vec3f(p, v, t); }
    if (sector == 3) { return vec3f(p, q, v); }
    if (sector == 4) { return vec3f(t, p, v); }
    return vec3f(v, p, q);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    var v: f32;

    let mode = i32(u.mode);
    if (mode == 1) {
        // Radial: distance from offset center
        let center = vec2f(0.5 + u.offset_x, 0.5 + u.offset_y);
        v = length(uv - center) * 2.0 * u.scale;
    } else {
        // Linear: dot product along angle direction
        let rad = u.angle * PI / 180.0;
        let dir = vec2f(cos(rad), sin(rad));
        let centered = uv - vec2f(0.5 + u.offset_x, 0.5 + u.offset_y);
        v = dot(centered, dir) * u.scale + 0.5;
    }

    // Repeat via fract
    v = fract(v * u.repeat);

    // Map to HSV color
    let hue = fract(u.hue_offset + v * u.hue_range);
    let rgb = hsv2rgb(vec3f(hue, u.saturation, u.brightness));
    return vec4f(rgb, 1.0);
}
