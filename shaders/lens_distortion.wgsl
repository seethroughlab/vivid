// Lens Distortion shader - barrel/pincushion distortion
// Uses uniforms:
//   u.param0 = distortion amount (-1 to 1, negative = pincushion, positive = barrel)
//   u.param1 = cubic distortion (additional k2 term for more complex lenses)
//   u.param2 = scale (zoom adjustment to fit distorted image, 0.5-1.5)
//   u.vec0.xy = center offset (0,0 = screen center)
//   u.mode = 0: Brown-Conrady model, 1: Simple polynomial, 2: Fisheye

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let k1 = u.param0;           // Primary distortion coefficient
    let k2 = u.param1;           // Secondary distortion coefficient
    let scale = u.param2;
    let centerOffset = u.vec0.xy;
    let mode = u.mode;

    // Normalize UV to -1 to 1 range, centered
    let center = vec2f(0.5, 0.5) + centerOffset;
    var uv = (in.uv - center) * 2.0;

    // Correct for aspect ratio
    let aspect = u.resolution.x / u.resolution.y;
    uv.x = uv.x * aspect;

    let r = length(uv);
    let r2 = r * r;
    let r4 = r2 * r2;

    var distortedUV: vec2f;

    if (mode == 0) {
        // Brown-Conrady model (used in camera calibration)
        let radialFactor = 1.0 + k1 * r2 + k2 * r4;
        distortedUV = uv * radialFactor;
    } else if (mode == 1) {
        // Simple polynomial (common in real-time applications)
        let radialFactor = 1.0 + k1 * r2;
        distortedUV = uv * radialFactor;
    } else {
        // Fisheye projection
        if (r > 0.0001) {
            let theta = atan(r);
            let newR = theta / r;
            let fisheyeFactor = mix(1.0, newR, abs(k1));
            distortedUV = uv * fisheyeFactor;
        } else {
            distortedUV = uv;
        }
    }

    // Apply scale and convert back to 0-1 range
    distortedUV = distortedUV * scale;
    distortedUV.x = distortedUV.x / aspect;
    let finalUV = distortedUV * 0.5 + center;

    // Check bounds
    if (finalUV.x < 0.0 || finalUV.x > 1.0 || finalUV.y < 0.0 || finalUV.y > 1.0) {
        return vec4f(0.0, 0.0, 0.0, 1.0);  // Black outside bounds
    }

    return textureSample(inputTexture, inputSampler, finalUV);
}
