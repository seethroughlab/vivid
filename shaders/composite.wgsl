// Composite/Blend shader
// Blends two textures using various blend modes
// Uses uniforms:
//   u.mode = blend mode (0=over, 1=add, 2=multiply, 3=screen, 4=difference)
//   u.param0 = mix/opacity amount (0-1)
//
// Inputs:
//   inputTexture = background (A)
//   inputTexture2 = foreground (B)

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let mode = u.mode;
    let opacity = u.param0;

    // Sample both textures
    let bg = textureSample(inputTexture, inputSampler, in.uv);
    let fg = textureSample(inputTexture2, inputSampler, in.uv);

    // Apply opacity to foreground
    let fgAlpha = fg.a * opacity;

    var result: vec4f;

    switch (mode) {
        case 0: {  // Alpha Over (standard compositing)
            // Porter-Duff "over" operation
            let outAlpha = fgAlpha + bg.a * (1.0 - fgAlpha);
            let outRgb = (fg.rgb * fgAlpha + bg.rgb * bg.a * (1.0 - fgAlpha)) / max(outAlpha, 0.001);
            result = vec4f(outRgb, outAlpha);
        }
        case 1: {  // Add (Linear Dodge)
            let blended = bg.rgb + fg.rgb * fgAlpha;
            result = vec4f(min(blended, vec3f(1.0)), max(bg.a, fgAlpha));
        }
        case 2: {  // Multiply
            let blended = mix(bg.rgb, bg.rgb * fg.rgb, fgAlpha);
            result = vec4f(blended, max(bg.a, fgAlpha));
        }
        case 3: {  // Screen
            let blended = mix(bg.rgb, 1.0 - (1.0 - bg.rgb) * (1.0 - fg.rgb), fgAlpha);
            result = vec4f(blended, max(bg.a, fgAlpha));
        }
        case 4: {  // Difference
            let blended = mix(bg.rgb, abs(bg.rgb - fg.rgb), fgAlpha);
            result = vec4f(blended, max(bg.a, fgAlpha));
        }
        default: {
            result = bg;
        }
    }

    return result;
}
