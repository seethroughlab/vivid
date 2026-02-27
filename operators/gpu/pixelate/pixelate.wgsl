@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let dims = u.resolution;
    let block = vec2f(u.size_x, u.size_y);
    let uv = floor(input.uv * dims / block) * block / dims;
    return textureSample(inputTex, texSampler, uv);
}
