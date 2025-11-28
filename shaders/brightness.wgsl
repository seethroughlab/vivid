// Brightness and Contrast shader
// Uses uniforms:
//   u.param0 = brightness (multiplier)
//   u.param1 = contrast (around 0.5 gray point)

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let brightness = u.param0;
    let contrast = u.param1;

    var color = textureSample(inputTexture, inputSampler, in.uv).rgb;

    // Apply contrast (around 0.5 gray point)
    color = (color - 0.5) * contrast + 0.5;

    // Apply brightness
    color = color * brightness;

    return vec4f(clamp(color, vec3f(0.0), vec3f(1.0)), 1.0);
}
