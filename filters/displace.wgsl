/*{
  "name": "Displace",
  "inputs": [
    {"name": "source"},
    {"name": "map"}
  ],
  "params": [
    {"name": "amount", "default": 0.1, "min": 0.0, "max": 1.0}
  ]
}*/
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let map = textureSample(inputTex1, texSampler, input.uv);
    let offset = (map.rg - 0.5) * 2.0 * u.amount;
    return textureSample(inputTex0, texSampler, input.uv + offset);
}
