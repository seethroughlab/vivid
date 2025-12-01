// Simple radial gradient background shader
// Creates a dark vignette effect

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let uv = in.uv;

    // Distance from center
    let center = vec2f(0.5, 0.5);
    let dist = length(uv - center);

    // Animated subtle movement
    let animCenter = vec2f(
        0.5 + sin(u.time * 0.2) * 0.05,
        0.5 + cos(u.time * 0.15) * 0.05
    );
    let animDist = length(uv - animCenter);

    // Dark blue center, near-black edges
    let innerColor = vec3f(0.08, 0.10, 0.18);
    let outerColor = vec3f(0.02, 0.02, 0.05);

    // Smooth vignette
    let t = smoothstep(0.0, 0.9, animDist);
    let color = mix(innerColor, outerColor, t);

    return vec4f(color, 1.0);
}
