@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Center UV to -0.5..0.5
    let centered = input.uv - vec2f(0.5);

    // 1. Barrel distortion
    let dist = dot(centered, centered);
    let distorted = input.uv + centered * dist * u.curvature;

    // Edge mask: black outside [0,1]
    if (distorted.x < 0.0 || distorted.x > 1.0 || distorted.y < 0.0 || distorted.y > 1.0) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    // 2. Chromatic aberration (radial offset from center)
    let ca_dir = centered * u.chromatic;
    let r = textureSample(inputTex, texSampler, distorted + ca_dir).r;
    let g = textureSample(inputTex, texSampler, distorted).g;
    let b = textureSample(inputTex, texSampler, distorted - ca_dir).b;
    var col = vec3f(r, g, b);

    // 3. Bloom: average of 4 offset neighbors blended in
    let texel = 1.0 / u.resolution;
    let bloom_sample = (
        textureSample(inputTex, texSampler, distorted + vec2f( texel.x,  texel.y)).rgb +
        textureSample(inputTex, texSampler, distorted + vec2f(-texel.x,  texel.y)).rgb +
        textureSample(inputTex, texSampler, distorted + vec2f( texel.x, -texel.y)).rgb +
        textureSample(inputTex, texSampler, distorted + vec2f(-texel.x, -texel.y)).rgb
    ) * 0.25;
    col = mix(col, bloom_sample, u.bloom);

    // 4. Scanlines
    let scan = sin(distorted.y * u.resolution.y * PI);
    let darkening = 1.0 - u.scanline_intensity * scan * scan;
    col = col * darkening;

    // 5. Vignette
    let vig_dist = length(centered);
    let vig = smoothstep(0.5, 0.5 - u.vignette * 0.25, vig_dist);
    col = col * vig;

    return vec4f(col, 1.0);
}
