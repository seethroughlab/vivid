// 2D Physics - Circle rendering with SDF
// Draws up to 4 circles at positions passed via uniforms
// Note: Runtime prepends Uniforms, VertexOutput, uniform binding, and vs_main
// User shaders only need to define fs_main and helper functions

// SDF for a circle
fn sdCircle(p: vec2f, center: vec2f, radius: f32) -> f32 {
    return length(p - center) - radius;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let uv = in.position.xy / u.resolution;
    let aspect = u.resolution.x / u.resolution.y;

    // Normalize coordinates with aspect ratio correction
    var p = uv * 2.0 - 1.0;
    p.x *= aspect;

    // Background color
    var col = vec3f(0.05, 0.05, 0.1);

    // Circle colors
    let colors = array<vec3f, 4>(
        vec3f(1.0, 0.3, 0.3),  // Red
        vec3f(0.3, 1.0, 0.3),  // Green
        vec3f(0.3, 0.3, 1.0),  // Blue
        vec3f(1.0, 1.0, 0.3)   // Yellow
    );

    // Circle positions (convert from [0,1] to [-aspect, aspect] x [-1, 1])
    let positions = array<vec2f, 4>(
        vec2f(u.param0 * 2.0 - 1.0, u.param1 * 2.0 - 1.0) * vec2f(aspect, 1.0),
        vec2f(u.param2 * 2.0 - 1.0, u.param3 * 2.0 - 1.0) * vec2f(aspect, 1.0),
        vec2f(u.param4 * 2.0 - 1.0, u.param5 * 2.0 - 1.0) * vec2f(aspect, 1.0),
        vec2f(u.param6 * 2.0 - 1.0, u.param7 * 2.0 - 1.0) * vec2f(aspect, 1.0)
    );

    // Circle radii
    let radii = array<f32, 4>(u.vec0.x, u.vec0.y, u.vec1.x, u.vec1.y);

    // Draw each circle
    for (var i = 0; i < 4; i++) {
        let d = sdCircle(p, positions[i], radii[i]);

        // Smooth fill with slight glow
        let fill = 1.0 - smoothstep(0.0, 0.02, d);
        let glow = 0.3 * exp(-10.0 * max(d, 0.0));

        col = mix(col, colors[i], fill + glow);
    }

    return vec4f(col, 1.0);
}
