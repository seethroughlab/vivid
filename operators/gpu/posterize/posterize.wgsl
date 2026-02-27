@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);
    let n = u.levels;
    return vec4f(floor(color.rgb * n) / n, color.a);
}
