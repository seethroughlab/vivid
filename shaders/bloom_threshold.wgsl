// Bloom Threshold shader - extracts bright areas
// Uses uniforms:
//   u.param0 = threshold (brightness cutoff, 0-1)
//   u.param1 = softness (soft knee, 0-1)

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let threshold = u.param0;
    let softness = u.param1;

    let color = textureSample(inputTexture, inputSampler, in.uv);

    // Calculate luminance
    let luminance = dot(color.rgb, vec3f(0.2126, 0.7152, 0.0722));

    // Soft thresholding (smooth transition)
    let knee = threshold * softness;
    let soft = clamp((luminance - threshold + knee) / (2.0 * knee + 0.00001), 0.0, 1.0);
    let contribution = soft * soft * (3.0 - 2.0 * soft);  // Smoothstep

    // Hard threshold fallback for sharp cutoff
    let hard = step(threshold, luminance);

    // Blend soft and hard based on softness
    let factor = mix(hard, contribution, softness);

    return vec4f(color.rgb * factor, 1.0);
}
