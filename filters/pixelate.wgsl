/*{
  "name": "Pixelate",
  "params": [
    {"name": "size_x", "default": 8.0, "min": 1.0, "max": 256.0, "display": "xy_pad"},
    {"name": "size_y", "default": 8.0, "min": 1.0, "max": 256.0, "display": "xy_pad"}
  ]
}*/
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let dims = u.resolution;
    let block = vec2f(u.size_x, u.size_y);
    let uv = floor(input.uv * dims / block) * block / dims;
    return textureSample(inputTex, texSampler, uv);
}
