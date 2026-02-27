@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let repeat = vec2f(u.repeat_x, u.repeat_y);
    let offset = vec2f(u.offset_x, u.offset_y);
    var uv = input.uv * repeat + offset;

    if (u.mirror > 0.5) {
        // Ping-pong: triangle wave maps 0-1-2 to 0-1-0
        uv = abs(fract(uv * 0.5) * 2.0 - 1.0);
    } else {
        uv = fract(uv);
    }

    return textureSample(inputTex, texSampler, uv);
}
