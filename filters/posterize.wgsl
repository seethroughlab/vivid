/*{
  "name": "Posterize",
  "params": [
    {"name": "levels", "default": 8.0, "min": 2.0, "max": 256.0}
  ]
}*/
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);
    let n = u.levels;
    return vec4f(floor(color.rgb * n) / n, color.a);
}
