// Composite/Blend shader
// Uses uniforms:
//   u.mode = blend mode (0=over, 1=add, 2=multiply, 3=screen, 4=difference)
//   u.param0 = mix amount

// Note: This shader uses inputTexture for texture A
// For texture B, we'll need a second input texture (future enhancement)
// For now, we just output the input with the mix effect

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let mode = u.mode;
    let mixAmount = u.param0;

    let a = textureSample(inputTexture, inputSampler, in.uv);

    // For now, since we only have one input, we'll just demonstrate the modes
    // by blending with a solid color or pattern
    // In a real implementation, we'd have a second texture input
    let b = vec4f(0.5, 0.5, 0.5, 1.0);

    var result: vec4f;

    switch (mode) {
        case 0: {  // Over (lerp)
            result = mix(a, b, mixAmount);
        }
        case 1: {  // Add
            result = a + b * mixAmount;
        }
        case 2: {  // Multiply
            result = mix(a, a * b, mixAmount);
        }
        case 3: {  // Screen
            result = mix(a, 1.0 - (1.0 - a) * (1.0 - b), mixAmount);
        }
        case 4: {  // Difference
            result = mix(a, abs(a - b), mixAmount);
        }
        default: {
            result = a;
        }
    }

    return vec4f(result.rgb, 1.0);
}
