/*{
  "name": "Tile",
  "params": [
    {"name": "repeat_x", "default": 2.0, "min": 0.1, "max": 32.0, "display": "xy_pad"},
    {"name": "repeat_y", "default": 2.0, "min": 0.1, "max": 32.0, "display": "xy_pad"},
    {"name": "offset_x", "default": 0.0, "min": 0.0, "max": 1.0, "display": "xy_pad"},
    {"name": "offset_y", "default": 0.0, "min": 0.0, "max": 1.0, "display": "xy_pad"},
    {"name": "mirror",   "default": 0.0, "min": 0.0, "max": 1.0}
  ]
}*/
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
