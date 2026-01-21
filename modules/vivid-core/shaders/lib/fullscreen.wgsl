// Vivid Core - Fullscreen Triangle Utilities
// Standard vertex shader for fullscreen effects

struct FullscreenOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

// Generate fullscreen triangle vertices from vertex index
// Uses oversized triangle technique (no vertex buffer needed)
// flipY: set true for standard texture sampling (top-left origin)
fn fullscreenTriangle(vertexIndex: u32, flipY: bool) -> FullscreenOutput {
    // Triangle vertices: (-1,-1), (3,-1), (-1,3)
    // This single triangle covers the entire [-1,1] clip space
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );

    var out: FullscreenOutput;
    let pos = positions[vertexIndex];
    out.position = vec4f(pos, 0.0, 1.0);

    // Convert clip space to UV [0,1]
    out.uv = pos * 0.5 + 0.5;

    // Flip Y for standard texture coordinates (top-left origin)
    if (flipY) {
        out.uv.y = 1.0 - out.uv.y;
    }

    return out;
}

// Alternative using bit manipulation (matches blit.wgsl pattern)
fn fullscreenTriangleBit(vertexIndex: u32, flipY: bool) -> FullscreenOutput {
    var out: FullscreenOutput;

    let x = f32(i32(vertexIndex & 1u) * 4 - 1);
    let y = f32(i32(vertexIndex >> 1u) * 4 - 1);

    out.position = vec4f(x, y, 0.0, 1.0);
    out.uv = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);

    if (!flipY) {
        out.uv.y = 1.0 - out.uv.y;
    }

    return out;
}
