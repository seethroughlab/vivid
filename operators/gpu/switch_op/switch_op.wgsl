fn sample_input(idx: i32, uv: vec2f) -> vec4f {
    if (idx == 0) { return textureSample(inputTex0, texSampler, uv); }
    else if (idx == 1) { return textureSample(inputTex1, texSampler, uv); }
    else if (idx == 2) { return textureSample(inputTex2, texSampler, uv); }
    else if (idx == 3) { return textureSample(inputTex3, texSampler, uv); }
    else if (idx == 4) { return textureSample(inputTex4, texSampler, uv); }
    else if (idx == 5) { return textureSample(inputTex5, texSampler, uv); }
    else if (idx == 6) { return textureSample(inputTex6, texSampler, uv); }
    else               { return textureSample(inputTex7, texSampler, uv); }
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let idx = clamp(i32(u.index), 0, 7);
    let current = sample_input(idx, input.uv);

    if (u.blend <= 0.0) {
        return current;
    }

    let next = sample_input(min(idx + 1, 7), input.uv);
    return mix(current, next, u.blend);
}
