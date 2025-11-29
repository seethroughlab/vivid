// Vignette shader - darkens edges of the image
// Uses uniforms:
//   u.param0 = intensity (strength of darkening, 0-2)
//   u.param1 = radius (where vignette starts, 0-2, larger = smaller vignette)
//   u.param2 = softness (falloff smoothness, 0-2)
//   u.vec0 = center offset (default 0,0 = center)

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let intensity = u.param0;
    let radius = u.param1;
    let softness = u.param2;
    let centerOffset = u.vec0;

    let color = textureSample(inputTexture, inputSampler, in.uv);

    // Calculate distance from center (adjusted for aspect ratio)
    let center = vec2f(0.5, 0.5) + centerOffset;
    let uv = in.uv - center;

    // Correct for aspect ratio
    let aspect = u.resolution.x / u.resolution.y;
    let correctedUV = vec2f(uv.x * aspect, uv.y);

    // Distance from center (normalized)
    let dist = length(correctedUV) * 2.0;

    // Vignette calculation with smooth falloff
    let vignette = 1.0 - smoothstep(radius - softness, radius + softness, dist);

    // Apply intensity
    let vignetteAmount = mix(1.0, vignette, intensity);

    return vec4f(color.rgb * vignetteAmount, color.a);
}
