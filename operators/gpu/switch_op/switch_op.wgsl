@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let idx = clamp(i32(u.index), 0, 3);
    let b = u.blend;

    // Sample the selected texture
    var current: vec4f;
    if (idx == 0) { current = textureSample(inputTex0, texSampler, input.uv); }
    else if (idx == 1) { current = textureSample(inputTex1, texSampler, input.uv); }
    else if (idx == 2) { current = textureSample(inputTex2, texSampler, input.uv); }
    else              { current = textureSample(inputTex3, texSampler, input.uv); }

    if (b <= 0.0) {
        return current;
    }

    // Sample the next texture for crossfade
    let next_idx = min(idx + 1, 3);
    var next: vec4f;
    if (next_idx == 0) { next = textureSample(inputTex0, texSampler, input.uv); }
    else if (next_idx == 1) { next = textureSample(inputTex1, texSampler, input.uv); }
    else if (next_idx == 2) { next = textureSample(inputTex2, texSampler, input.uv); }
    else                    { next = textureSample(inputTex3, texSampler, input.uv); }

    return mix(current, next, b);
}
