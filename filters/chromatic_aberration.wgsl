/*{
  "name": "ChromaticAberration",
  "params": [
    {"name": "amount", "default": 0.01, "min": 0.0, "max": 0.1},
    {"name": "angle",  "default": 0.0,  "min": 0.0, "max": 360.0},
    {"name": "radial", "default": 0.0,  "min": 0.0, "max": 1.0}
  ]
}*/
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let rad = u.angle * PI / 180.0;
    var dir = vec2f(cos(rad), sin(rad));

    if (u.radial > 0.5) {
        // Radial mode: direction points away from center, scaled by distance
        let center = input.uv - vec2f(0.5);
        dir = center;
    }

    let offset = dir * u.amount;

    let r = textureSample(inputTex, texSampler, input.uv + offset).r;
    let center_sample = textureSample(inputTex, texSampler, input.uv);
    let b = textureSample(inputTex, texSampler, input.uv - offset).b;

    return vec4f(r, center_sample.g, b, center_sample.a);
}
