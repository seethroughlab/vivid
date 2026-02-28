/*{
  "name": "Transform",
  "params": [
    {"name": "scale_x",     "default": 1.0, "min": 0.01, "max": 10.0, "display": "xy_pad"},
    {"name": "scale_y",     "default": 1.0, "min": 0.01, "max": 10.0, "display": "xy_pad"},
    {"name": "rotation",    "default": 0.0, "min": 0.0,  "max": 360.0},
    {"name": "translate_x", "default": 0.0, "min": -1.0, "max": 1.0, "display": "xy_pad"},
    {"name": "translate_y", "default": 0.0, "min": -1.0, "max": 1.0, "display": "xy_pad"},
    {"name": "pivot_x",     "default": 0.5, "min": 0.0,  "max": 1.0, "display": "xy_pad"},
    {"name": "pivot_y",     "default": 0.5, "min": 0.0,  "max": 1.0, "display": "xy_pad"}
  ]
}*/
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let pivot = vec2f(u.pivot_x, u.pivot_y);

    // Inverse transform: output UV → input UV
    var p = input.uv - pivot;

    // Inverse rotation
    let rad = -u.rotation * PI / 180.0;
    let c = cos(rad);
    let s = sin(rad);
    p = vec2f(c * p.x - s * p.y, s * p.x + c * p.y);

    // Inverse scale
    p = p / vec2f(u.scale_x, u.scale_y);

    // Restore pivot and apply inverse translation
    let sample_uv = p + pivot - vec2f(u.translate_x, u.translate_y);

    // Return transparent black for out-of-bounds
    if (sample_uv.x < 0.0 || sample_uv.x > 1.0 || sample_uv.y < 0.0 || sample_uv.y > 1.0) {
        return vec4f(0.0, 0.0, 0.0, 0.0);
    }

    return textureSample(inputTex, texSampler, sample_uv);
}
