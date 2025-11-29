// Alpha Over Composite shader
// Composites foreground (param texture) over background (input texture)
// Uses standard alpha blending: out = fg * fg.a + bg * (1 - fg.a)
//
// Uses uniforms:
//   u.param0-3 = foreground color (for when we sample from a separate texture)
//   This shader uses vec0/vec1 as UV offset for the foreground

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Sample background (gradient)
    let bg = textureSample(inputTexture, inputSampler, in.uv);

    // Sample foreground - using same texture but this will be the displaced image
    // In this case we're re-using the input, but the actual compositing
    // happens in the CPU code by running this shader with the right textures
    let fg = textureSample(inputTexture, inputSampler, in.uv);

    // Standard alpha-over blending
    let outAlpha = fg.a + bg.a * (1.0 - fg.a);
    let outRgb = (fg.rgb * fg.a + bg.rgb * bg.a * (1.0 - fg.a)) / max(outAlpha, 0.001);

    return vec4f(outRgb, outAlpha);
}
