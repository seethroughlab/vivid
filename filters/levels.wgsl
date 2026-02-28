/*{
  "name": "Levels",
  "params": [
    {"name": "brightness", "default": 0.0, "min": -1.0, "max": 1.0},
    {"name": "contrast",   "default": 1.0, "min": 0.0,  "max": 3.0},
    {"name": "gamma",      "default": 1.0, "min": 0.1,  "max": 5.0}
  ]
}*/
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);

    // Brightness
    var rgb = color.rgb + vec3f(u.brightness);

    // Contrast (pivot at 0.5)
    rgb = (rgb - 0.5) * u.contrast + 0.5;

    // Gamma correction
    rgb = pow(clamp(rgb, vec3f(0.0), vec3f(1.0)), vec3f(1.0 / u.gamma));

    return vec4f(rgb, color.a);
}
