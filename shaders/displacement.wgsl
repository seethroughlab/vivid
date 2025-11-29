// Displacement shader
// Distorts source texture using a separate displacement map texture
// Uses uniforms:
//   u.mode = channel mode (0=luminance, 1=R, 2=G, 3=RG for x/y)
//   u.param0 = amount (displacement strength)
//   u.vec0 = direction multiplier (for directional displacement)
//
// Inputs:
//   inputTexture = source texture to distort
//   inputTexture2 = displacement map (any texture - noise, gradient, image, etc.)

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let mode = u.mode;
    let amount = u.param0;
    let direction = u.vec0;

    // Sample the displacement map (inputTexture2)
    let dispSample = textureSample(inputTexture2, inputSampler, in.uv);

    var displacement: vec2f;

    switch (mode) {
        case 0: {  // Luminance
            let lum = dot(dispSample.rgb, vec3f(0.299, 0.587, 0.114));
            displacement = vec2f(lum - 0.5) * 2.0;
        }
        case 1: {  // Red channel
            displacement = vec2f(dispSample.r - 0.5) * 2.0;
        }
        case 2: {  // Green channel
            displacement = vec2f(dispSample.g - 0.5) * 2.0;
        }
        case 3: {  // RG channels for X/Y
            displacement = (dispSample.rg - 0.5) * 2.0;
        }
        default: {
            displacement = vec2f(0.0);
        }
    }

    // Apply direction multiplier
    displacement = displacement * direction;

    // Calculate displaced UV
    let displacedUv = in.uv + displacement * amount;

    // Sample source texture with displaced coordinates
    return textureSample(inputTexture, inputSampler, displacedUv);
}
