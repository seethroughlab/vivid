// Radial Blur shader - zoom/speed blur from center
// Uses uniforms:
//   u.param0 = intensity (blur strength, 0-1)
//   u.param1 = samples (number of samples, 4-32)
//   u.vec0.xy = center point (0.5, 0.5 = screen center)
//   u.mode = 0: zoom blur (outward), 1: spin blur (rotational)

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let intensity = u.param0;
    let sampleCount = i32(clamp(u.param1, 4.0, 32.0));
    let center = u.vec0.xy;
    let mode = u.mode;

    let uv = in.uv;
    let toCenter = uv - center;
    let dist = length(toCenter);

    var totalColor = vec4f(0.0);
    let samplesF = f32(sampleCount);

    if (mode == 0) {
        // Zoom blur: sample along ray from center
        for (var i = 0; i < sampleCount; i = i + 1) {
            let t = f32(i) / samplesF;
            let scale = 1.0 - t * intensity * 0.5;  // Gradually move toward center
            let sampleUV = center + toCenter * scale;
            totalColor = totalColor + textureSample(inputTexture, inputSampler, sampleUV);
        }
    } else {
        // Spin blur: sample along arc around center
        let angle = atan2(toCenter.y, toCenter.x);
        for (var i = 0; i < sampleCount; i = i + 1) {
            let t = (f32(i) / samplesF - 0.5) * 2.0;  // -1 to 1
            let angleOffset = t * intensity * 0.3 * dist;  // More blur at edges
            let newAngle = angle + angleOffset;
            let sampleUV = center + vec2f(cos(newAngle), sin(newAngle)) * dist;
            totalColor = totalColor + textureSample(inputTexture, inputSampler, sampleUV);
        }
    }

    return totalColor / samplesF;
}
