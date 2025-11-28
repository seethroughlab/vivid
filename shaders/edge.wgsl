// Edge Detection shader (Sobel)
// Uses uniforms:
//   u.param0 = threshold (edge sensitivity)
//   u.param1 = thickness (sample distance multiplier)
//   u.mode = output mode (0=edges only, 1=edges+original, 2=inverted)

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let threshold = u.param0;
    let thickness = u.param1;
    let mode = u.mode;

    let pixelSize = thickness / u.resolution;

    // Sample 3x3 neighborhood
    let tl = dot(textureSample(inputTexture, inputSampler, in.uv + vec2f(-pixelSize.x, -pixelSize.y)).rgb, vec3f(0.299, 0.587, 0.114));
    let tc = dot(textureSample(inputTexture, inputSampler, in.uv + vec2f(0.0, -pixelSize.y)).rgb, vec3f(0.299, 0.587, 0.114));
    let tr = dot(textureSample(inputTexture, inputSampler, in.uv + vec2f(pixelSize.x, -pixelSize.y)).rgb, vec3f(0.299, 0.587, 0.114));

    let ml = dot(textureSample(inputTexture, inputSampler, in.uv + vec2f(-pixelSize.x, 0.0)).rgb, vec3f(0.299, 0.587, 0.114));
    let mr = dot(textureSample(inputTexture, inputSampler, in.uv + vec2f(pixelSize.x, 0.0)).rgb, vec3f(0.299, 0.587, 0.114));

    let bl = dot(textureSample(inputTexture, inputSampler, in.uv + vec2f(-pixelSize.x, pixelSize.y)).rgb, vec3f(0.299, 0.587, 0.114));
    let bc = dot(textureSample(inputTexture, inputSampler, in.uv + vec2f(0.0, pixelSize.y)).rgb, vec3f(0.299, 0.587, 0.114));
    let br = dot(textureSample(inputTexture, inputSampler, in.uv + vec2f(pixelSize.x, pixelSize.y)).rgb, vec3f(0.299, 0.587, 0.114));

    // Sobel kernels
    let gx = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
    let gy = -tl - 2.0*tc - tr + bl + 2.0*bc + br;

    // Edge magnitude
    var edge = sqrt(gx*gx + gy*gy);

    // Apply threshold
    edge = smoothstep(threshold, threshold + 0.1, edge);

    let original = textureSample(inputTexture, inputSampler, in.uv);

    switch (mode) {
        case 0: {  // Edges only (white on black)
            return vec4f(edge, edge, edge, 1.0);
        }
        case 1: {  // Edges + original
            return vec4f(original.rgb + vec3f(edge), 1.0);
        }
        case 2: {  // Inverted (black edges on original)
            return vec4f(original.rgb * (1.0 - edge), 1.0);
        }
        default: {
            return vec4f(edge, edge, edge, 1.0);
        }
    }
}
