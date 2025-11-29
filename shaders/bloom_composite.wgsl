// Bloom Composite shader - combines original with blurred bright areas
// Uses uniforms:
//   u.param0 = intensity (bloom strength, 0-2)
//   u.param1 = mix (0 = original only, 1 = bloom only)
// Inputs:
//   inputTexture = original image
//   inputTexture2 = blurred bloom texture

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let intensity = u.param0;
    let mixAmount = u.param1;

    let original = textureSample(inputTexture, inputSampler, in.uv);
    let bloom = textureSample(inputTexture2, inputSampler, in.uv);

    // Additive blending with intensity control
    let bloomed = original.rgb + bloom.rgb * intensity;

    // Optional mix between original and bloomed
    let result = mix(original.rgb, bloomed, mixAmount);

    return vec4f(result, original.a);
}
