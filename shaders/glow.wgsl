// Glow shader - applies glow effect based on brightness or alpha mask
// Uses uniforms:
//   u.param0 = threshold (brightness threshold for glow, 0-1)
//   u.param1 = intensity (glow strength, 0-2)
//   u.param2 = radius (glow spread, 0.001-0.1)
//   u.param3 = softness (glow falloff, 0.5-2)
//   u.vec0.rgb = glow color tint (1,1,1 = use source color)
//   u.mode = 0: brightness-based, 1: alpha-based (for masked glow), 2: additive only

// Get luminance
fn getLuminance(c: vec3f) -> f32 {
    return dot(c, vec3f(0.2126, 0.7152, 0.0722));
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let threshold = u.param0;
    let intensity = u.param1;
    let radius = u.param2;
    let softness = u.param3;
    let glowTint = vec3f(u.vec0.x, u.vec0.y, u.vec1.x);  // Using vec0.xy and vec1.x for rgb
    let mode = u.mode;

    let original = textureSample(inputTexture, inputSampler, in.uv);

    // Multi-sample blur for glow
    var glowColor = vec3f(0.0);
    var totalWeight = 0.0;

    let samples = 16;
    let goldenAngle = 2.39996323;  // Golden angle in radians

    for (var i = 0; i < samples; i = i + 1) {
        let t = f32(i) / f32(samples);
        let r = sqrt(t) * radius;  // Fibonacci spiral distribution
        let angle = f32(i) * goldenAngle;
        let offset = vec2f(cos(angle), sin(angle)) * r;

        let sampleUV = in.uv + offset;
        let sample = textureSample(inputTexture, inputSampler, sampleUV);

        var glowStrength: f32;
        if (mode == 1) {
            // Alpha-based: use alpha channel as glow mask
            glowStrength = sample.a;
        } else {
            // Brightness-based: threshold luminance
            let lum = getLuminance(sample.rgb);
            glowStrength = smoothstep(threshold, threshold + 0.1, lum);
        }

        // Distance-based weight for softer glow
        let dist = length(offset) / radius;
        let weight = pow(1.0 - dist, softness);

        glowColor += sample.rgb * glowStrength * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.0) {
        glowColor = glowColor / totalWeight;
    }

    // Apply glow tint
    glowColor = glowColor * glowTint;

    // Combine with original
    var result: vec3f;
    if (mode == 2) {
        // Additive only (for compositing)
        result = glowColor * intensity;
    } else {
        // Add glow to original
        result = original.rgb + glowColor * intensity;
    }

    return vec4f(result, original.a);
}
