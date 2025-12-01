// Film Grain shader - animated noise overlay for cinematic look
// Uses uniforms:
//   u.param0 = intensity (grain strength, 0-1)
//   u.param1 = size (grain size, 1-10, higher = finer grain)
//   u.param2 = speed (animation speed multiplier)
//   u.param3 = luminance_response (0 = uniform, 1 = stronger in midtones)
//   u.time = used for animation

// Fast hash function for pseudo-random noise
fn hash21(p: vec2f) -> f32 {
    var p3 = fract(vec3f(p.xyx) * 0.1031);
    p3 = p3 + dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Value noise with interpolation
fn noise(p: vec2f) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);

    return mix(
        mix(hash21(i + vec2f(0.0, 0.0)), hash21(i + vec2f(1.0, 0.0)), u.x),
        mix(hash21(i + vec2f(0.0, 1.0)), hash21(i + vec2f(1.0, 1.0)), u.x),
        u.y
    );
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let intensity = u.param0;
    let size = max(u.param1, 1.0);
    let speed = u.param2;
    let luminanceResponse = u.param3;

    let color = textureSample(inputTexture, inputSampler, in.uv);

    // Generate animated grain
    let grainCoord = in.uv * u.resolution * size * 0.1;
    let timeOffset = floor(u.time * 24.0 * speed);  // 24fps grain animation

    // Multiple octaves for more natural film grain
    var grain = noise(grainCoord + vec2f(timeOffset * 123.456, timeOffset * 789.012));
    grain += noise(grainCoord * 2.0 + vec2f(timeOffset * 456.789, timeOffset * 234.567)) * 0.5;
    grain += noise(grainCoord * 4.0 + vec2f(timeOffset * 111.222, timeOffset * 333.444)) * 0.25;
    grain = grain / 1.75;  // Normalize
    grain = grain * 2.0 - 1.0;  // Center around 0 (-1 to 1)

    // Calculate luminance for response curve
    let luminance = dot(color.rgb, vec3f(0.2126, 0.7152, 0.0722));

    // Luminance-based response: grain is stronger in midtones, weaker in shadows/highlights
    let midtoneWeight = 1.0 - abs(luminance - 0.5) * 2.0;
    let responseMultiplier = mix(1.0, midtoneWeight, luminanceResponse);

    // Apply grain
    let grainAmount = grain * intensity * responseMultiplier * 0.15;
    let result = color.rgb + vec3f(grainAmount);

    return vec4f(clamp(result, vec3f(0.0), vec3f(1.0)), color.a);
}
