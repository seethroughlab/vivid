const TAU: f32 = 6.28318530718;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let center = vec2f(u.center_x, u.center_y);
    var uv = input.uv;

    let mode = i32(u.mode);
    if (mode == 3) {
        // Kaleidoscope
        let p = uv - center;
        let rot = u.angle * 3.14159265 / 180.0;
        var a = atan2(p.y, p.x) + rot;
        let r = length(p);

        let slice = TAU / u.segments;
        a = a % slice;
        if (a < 0.0) { a += slice; }

        // Reflect odd segments
        if (a > slice * 0.5) {
            a = slice - a;
        }

        uv = center + vec2f(cos(a), sin(a)) * r;
    } else if (mode == 2) {
        // Quad
        uv = center + abs(uv - center);
    } else if (mode == 1) {
        // Vertical
        uv.y = center.y + abs(uv.y - center.y);
    } else {
        // Horizontal
        uv.x = center.x + abs(uv.x - center.x);
    }

    uv = clamp(uv, vec2f(0.0), vec2f(1.0));
    return textureSample(inputTex, texSampler, uv);
}
