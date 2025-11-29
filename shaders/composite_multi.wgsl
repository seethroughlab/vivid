// Multi-input Composite shader
// Blends up to 8 input textures using alpha over compositing
// Uses uniforms:
//   u.mode = number of active inputs (1-8)
//   u.param0 = overall opacity (0-1)
//
// Inputs are composited in order: inputTexture (background), inputTexture2, inputTexture3, etc.
// Each layer is blended on top using Porter-Duff "over" operation

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let numInputs = u.mode;
    let opacity = u.param0;

    // Start with first input as background
    var result = textureSample(inputTexture, inputSampler, in.uv);

    // Composite each additional layer
    if (numInputs >= 2) {
        let layer = textureSample(inputTexture2, inputSampler, in.uv);
        result = alphaOver(result, layer);
    }
    if (numInputs >= 3) {
        let layer = textureSample(inputTexture3, inputSampler, in.uv);
        result = alphaOver(result, layer);
    }
    if (numInputs >= 4) {
        let layer = textureSample(inputTexture4, inputSampler, in.uv);
        result = alphaOver(result, layer);
    }
    if (numInputs >= 5) {
        let layer = textureSample(inputTexture5, inputSampler, in.uv);
        result = alphaOver(result, layer);
    }
    if (numInputs >= 6) {
        let layer = textureSample(inputTexture6, inputSampler, in.uv);
        result = alphaOver(result, layer);
    }
    if (numInputs >= 7) {
        let layer = textureSample(inputTexture7, inputSampler, in.uv);
        result = alphaOver(result, layer);
    }
    if (numInputs >= 8) {
        let layer = textureSample(inputTexture8, inputSampler, in.uv);
        result = alphaOver(result, layer);
    }

    // Apply overall opacity
    result.a *= opacity;

    return result;
}

// Porter-Duff "over" operation
fn alphaOver(bg: vec4f, fg: vec4f) -> vec4f {
    let outAlpha = fg.a + bg.a * (1.0 - fg.a);
    if (outAlpha < 0.001) {
        return vec4f(0.0);
    }
    let outRgb = (fg.rgb * fg.a + bg.rgb * bg.a * (1.0 - fg.a)) / outAlpha;
    return vec4f(outRgb, outAlpha);
}
