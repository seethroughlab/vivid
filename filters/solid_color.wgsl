/*{
  "name": "SolidColor",
  "inputs": [],
  "params": [
    {"name": "r", "default": 1.0, "min": 0.0, "max": 1.0},
    {"name": "g", "default": 1.0, "min": 0.0, "max": 1.0},
    {"name": "b", "default": 1.0, "min": 0.0, "max": 1.0},
    {"name": "a", "default": 1.0, "min": 0.0, "max": 1.0}
  ]
}*/
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return vec4f(u.r, u.g, u.b, u.a);
}
