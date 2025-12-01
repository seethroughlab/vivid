// Motion Blur shader - directional or camera motion blur
// Note: For per-pixel velocity-based blur, you would need a velocity buffer from 3D rendering
// This shader provides screen-space approximations
// Uses uniforms:
//   u.param0 = intensity (blur amount, 0-1)
//   u.param1 = samples (number of samples, 4-16)
//   u.vec0.xy = direction (motion direction, e.g., (1,0) for horizontal)
//   u.mode = 0: directional (linear), 1: camera shake, 2: object motion (needs velocity texture)

// Simple hash for pseudo-random jitter
fn hash12(p: vec2f) -> f32 {
    var p3 = fract(vec3f(p.xyx) * 0.1031);
    p3 = p3 + dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let intensity = u.param0;
    let sampleCount = i32(clamp(u.param1, 4.0, 16.0));
    let direction = normalize(u.vec0.xy + vec2f(0.0001));  // Avoid zero vector
    let mode = u.mode;

    var totalColor = vec4f(0.0);
    let samplesF = f32(sampleCount);

    if (mode == 0) {
        // Directional motion blur (linear)
        let blurDir = direction * intensity * 0.1;

        for (var i = 0; i < sampleCount; i = i + 1) {
            let t = (f32(i) / samplesF - 0.5) * 2.0;  // -1 to 1
            let offset = blurDir * t;
            totalColor = totalColor + textureSample(inputTexture, inputSampler, in.uv + offset);
        }
    } else if (mode == 1) {
        // Camera shake blur (multiple random directions)
        let seed = in.uv * 100.0 + vec2f(u.time);

        for (var i = 0; i < sampleCount; i = i + 1) {
            let angle = hash12(seed + vec2f(f32(i) * 1.234)) * 6.28318;
            let dist = hash12(seed + vec2f(f32(i) * 5.678)) * intensity * 0.05;
            let offset = vec2f(cos(angle), sin(angle)) * dist;
            totalColor = totalColor + textureSample(inputTexture, inputSampler, in.uv + offset);
        }
    } else {
        // Object motion simulation (radial from implied motion center)
        // Without actual velocity data, simulate as if camera is moving forward
        let center = vec2f(0.5, 0.5);
        let toPixel = in.uv - center;
        let dist = length(toPixel);

        // Motion increases with distance from center (parallax effect)
        let motionDir = normalize(toPixel + vec2f(0.0001)) * dist;
        let blurAmount = intensity * 0.1 * dist;

        for (var i = 0; i < sampleCount; i = i + 1) {
            let t = (f32(i) / samplesF - 0.5) * 2.0;
            let offset = motionDir * blurAmount * t;
            totalColor = totalColor + textureSample(inputTexture, inputSampler, in.uv + offset);
        }
    }

    return totalColor / samplesF;
}
