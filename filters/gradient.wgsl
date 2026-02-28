/*{
  "name": "Gradient",
  "inputs": [],
  "params": [
    {"name": "mode",     "type": "int", "default": 0, "min": 0, "max": 1,
     "choices": ["Linear", "Radial"]},
    {"name": "angle",    "default": 0.0,   "min": 0.0, "max": 360.0},
    {"name": "center_x", "default": 0.5,   "min": 0.0, "max": 1.0, "display": "xy_pad"},
    {"name": "center_y", "default": 0.5,   "min": 0.0, "max": 1.0, "display": "xy_pad"},
    {"name": "scale",    "default": 1.0,   "min": 0.1, "max": 10.0},
    {"name": "offset",   "default": 0.0,   "min": -1.0, "max": 1.0}
  ]
}*/
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    var v: f32;

    let mode = i32(u.mode);
    if (mode == 1) {
        // Radial
        let center = vec2f(u.center_x, u.center_y);
        v = length(uv - center) * 2.0 * u.scale + u.offset;
    } else {
        // Linear
        let rad = u.angle * PI / 180.0;
        let dir = vec2f(cos(rad), sin(rad));
        v = dot(uv - vec2f(0.5), dir) * u.scale + 0.5 + u.offset;
    }

    v = clamp(v, 0.0, 1.0);
    return vec4f(v, v, v, 1.0);
}
