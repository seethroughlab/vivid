// Lens Flare shader - generates anamorphic lens flare from bright spots
// Uses uniforms:
//   u.param0 = threshold (brightness threshold for flare sources, 0-1)
//   u.param1 = intensity (flare strength, 0-2)
//   u.param2 = streak_length (horizontal streak extent, 0-1)
//   u.param3 = ghost_intensity (circular ghost artifacts, 0-1)
//   u.vec0.xy = light source position (0.5, 0.5 = center), or use (0,0) for auto-detect
//   u.mode = 0: anamorphic horizontal streaks, 1: circular ghosts, 2: full cinematic

// Get luminance
fn getLuminance(c: vec3f) -> f32 {
    return dot(c, vec3f(0.2126, 0.7152, 0.0722));
}

// Smooth threshold function
fn softThreshold(value: f32, threshold: f32) -> f32 {
    return smoothstep(threshold, threshold + 0.1, value);
}

// Generate anamorphic streak
fn anamorphicStreak(uv: vec2f, sourceUV: vec2f, brightness: f32, streakLength: f32) -> vec3f {
    let yDist = abs(uv.y - sourceUV.y);
    let xDist = uv.x - sourceUV.x;

    // Horizontal falloff
    let xFalloff = exp(-abs(xDist) / (streakLength + 0.001) * 3.0);
    // Vertical falloff (sharp)
    let yFalloff = exp(-yDist * 100.0);

    let streak = xFalloff * yFalloff * brightness;

    // Chromatic dispersion - shift colors along streak
    let r = streak * smoothstep(-0.3, 0.0, xDist);
    let g = streak;
    let b = streak * smoothstep(0.0, 0.3, -xDist);

    return vec3f(r, g * 0.8, b) * vec3f(1.0, 0.9, 0.7);  // Warm tint
}

// Generate circular ghost artifact
fn ghostCircle(uv: vec2f, center: vec2f, radius: f32, brightness: f32) -> vec3f {
    let dist = length(uv - center);
    let ring = smoothstep(radius - 0.02, radius, dist) * smoothstep(radius + 0.02, radius, dist);
    let fill = smoothstep(radius, 0.0, dist) * 0.3;
    return vec3f(ring + fill) * brightness * vec3f(0.5, 0.7, 1.0);  // Cool tint for ghosts
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let threshold = u.param0;
    let intensity = u.param1;
    let streakLength = u.param2;
    let ghostIntensity = u.param3;
    let lightPos = u.vec0.xy;
    let mode = u.mode;

    let original = textureSample(inputTexture, inputSampler, in.uv);

    var flareColor = vec3f(0.0);

    // Sample bright areas for flare sources
    let centerSample = textureSample(inputTexture, inputSampler, lightPos);
    let centerBrightness = softThreshold(getLuminance(centerSample.rgb), threshold);

    if (mode == 0 || mode == 2) {
        // Anamorphic streaks
        if (lightPos.x > 0.001 || lightPos.y > 0.001) {
            // Use specified light position
            flareColor += anamorphicStreak(in.uv, lightPos, centerBrightness, streakLength);
        } else {
            // Auto-detect: sample multiple bright spots
            for (var y = 0; y < 4; y = y + 1) {
                for (var x = 0; x < 4; x = x + 1) {
                    let sampleUV = vec2f(f32(x) + 0.5, f32(y) + 0.5) / 4.0;
                    let sample = textureSample(inputTexture, inputSampler, sampleUV);
                    let brightness = softThreshold(getLuminance(sample.rgb), threshold);
                    if (brightness > 0.01) {
                        flareColor += anamorphicStreak(in.uv, sampleUV, brightness * 0.5, streakLength);
                    }
                }
            }
        }
    }

    if (mode == 1 || mode == 2) {
        // Circular ghosts (reflections through lens elements)
        let center = vec2f(0.5, 0.5);
        let toLight = lightPos - center;

        // Create multiple ghost reflections at different scales
        for (var i = 1; i < 5; i = i + 1) {
            let scale = 1.0 - f32(i) * 0.3;
            let ghostPos = center - toLight * scale;
            let radius = 0.02 + f32(i) * 0.015;
            flareColor += ghostCircle(in.uv, ghostPos, radius, centerBrightness * ghostIntensity * 0.3);
        }

        // Central glow around light source
        let distToLight = length(in.uv - lightPos);
        let glow = exp(-distToLight * 8.0) * centerBrightness;
        flareColor += vec3f(glow) * vec3f(1.0, 0.95, 0.8);
    }

    let result = original.rgb + flareColor * intensity;
    return vec4f(result, original.a);
}
