// Demo scene shader - animated gradient with bright spots for testing post-processing
// Creates an interesting base image with high dynamic range elements

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let uv = in.uv;
    let t = u.time;

    // Base gradient
    let baseColor = mix(
        vec3f(0.1, 0.15, 0.3),   // Dark blue
        vec3f(0.4, 0.2, 0.1),    // Warm brown
        uv.y
    );

    // Animated swirling pattern
    let swirl = sin(uv.x * 10.0 + t) * cos(uv.y * 8.0 - t * 0.7) * 0.5 + 0.5;
    let pattern = mix(baseColor, vec3f(0.3, 0.4, 0.5), swirl * 0.3);

    // Add some bright spots for bloom/glow testing
    var brightness = 0.0;

    // Moving bright spot 1
    let spot1 = vec2f(0.5 + sin(t * 0.5) * 0.3, 0.5 + cos(t * 0.7) * 0.2);
    let dist1 = length(uv - spot1);
    brightness += exp(-dist1 * 15.0) * 2.0;

    // Moving bright spot 2
    let spot2 = vec2f(0.3 + cos(t * 0.8) * 0.2, 0.7 + sin(t * 0.6) * 0.15);
    let dist2 = length(uv - spot2);
    brightness += exp(-dist2 * 20.0) * 1.5;

    // Static bright corner for lens flare testing
    let corner = vec2f(0.9, 0.1);
    let distCorner = length(uv - corner);
    brightness += exp(-distCorner * 10.0) * 1.8;

    // Subtle grid lines
    let gridX = smoothstep(0.98, 1.0, fract(uv.x * 20.0)) * 0.1;
    let gridY = smoothstep(0.98, 1.0, fract(uv.y * 20.0)) * 0.1;

    // Combine
    var color = pattern + vec3f(gridX + gridY);
    color = color + vec3f(1.0, 0.95, 0.8) * brightness;  // Warm bright spots

    // Add some color variation
    let colorShift = sin(uv.x * 5.0 + t * 0.3) * cos(uv.y * 4.0 - t * 0.4);
    color.r = color.r + colorShift * 0.05;
    color.b = color.b - colorShift * 0.05;

    return vec4f(color, 1.0);
}
