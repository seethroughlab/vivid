// Blit shader - renders a texture to the screen using a full-screen triangle
// Shows checkerboard pattern for transparent areas (like Photoshop)

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

// Full-screen triangle that covers the viewport
// Uses vertex ID to generate positions (no vertex buffer needed)
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;

    // Generate a full-screen triangle
    // Vertex 0: (-1, -1) -> UV (0, 1)
    // Vertex 1: ( 3, -1) -> UV (2, 1)
    // Vertex 2: (-1,  3) -> UV (0, -1)
    let x = f32(i32(vertexIndex & 1u) * 4 - 1);
    let y = f32(i32(vertexIndex >> 1u) * 4 - 1);

    output.position = vec4f(x, y, 0.0, 1.0);
    output.uv = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);

    return output;
}

@group(0) @binding(0) var textureSampler: sampler;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;

// Generate checkerboard pattern for transparent areas
fn checkerboard(pos: vec2f) -> vec3f {
    let size = 8.0;  // Checker size in pixels
    let checker = floor(pos.x / size) + floor(pos.y / size);
    let isLight = (i32(checker) & 1) == 0;
    return select(vec3f(0.3, 0.3, 0.3), vec3f(0.5, 0.5, 0.5), isLight);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let texColor = textureSample(inputTexture, textureSampler, input.uv);

    // Blend texture over checkerboard pattern based on alpha
    let bg = checkerboard(input.position.xy);
    let blended = mix(bg, texColor.rgb, texColor.a);

    return vec4f(blended, 1.0);
}
