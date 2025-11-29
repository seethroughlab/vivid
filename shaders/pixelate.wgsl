// Pixelate shader
// Reduces effective resolution for blocky/mosaic effect
// Uses uniforms:
//   u.param0 = blockSize (pixels per block, 1-64)
//   u.mode = 0: square blocks, 1: aspect-corrected blocks

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let blockSize = max(u.param0, 1.0);
    let mode = u.mode;

    var pixelCount: vec2f;

    if (mode == 0) {
        // Square blocks (may stretch on non-square textures)
        pixelCount = u.resolution / blockSize;
    } else {
        // Aspect-corrected blocks
        let aspect = u.resolution.x / u.resolution.y;
        pixelCount = vec2f(
            u.resolution.x / blockSize,
            u.resolution.y / (blockSize / aspect)
        );
    }

    // Snap UV to block grid
    let blockUV = floor(in.uv * pixelCount) / pixelCount;

    // Sample from center of block for better quality
    let centerOffset = 0.5 / pixelCount;
    let sampleUV = blockUV + centerOffset;

    return textureSample(inputTexture, inputSampler, sampleUV);
}
