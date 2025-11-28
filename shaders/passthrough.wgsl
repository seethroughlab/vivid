// Passthrough shader - copies input texture to output unchanged

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return textureSample(inputTexture, inputSampler, in.uv);
}
