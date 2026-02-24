#pragma once

// Shared WGSL snippets for GPU operators and the runtime blit pass.
// Operators concatenate these with their own fragment shaders.

namespace vivid::gpu {

// Fullscreen triangle vertex shader — covers the entire viewport with a single
// oversized triangle.  No vertex buffer needed; just draw 3 vertices.
// Provides `FullscreenOutput` with position and UV (top-left origin, Y-flipped).
inline constexpr const char* FULLSCREEN_VERTEX_WGSL = R"(
struct FullscreenOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

fn fullscreenTriangle(vertexIndex: u32, flipY: bool) -> FullscreenOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: FullscreenOutput;
    let pos = positions[vertexIndex];
    out.position = vec4f(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    if (flipY) {
        out.uv.y = 1.0 - out.uv.y;
    }
    return out;
}
)";

// Common WGSL math constants.
inline constexpr const char* WGSL_CONSTANTS = R"(
const PI:    f32 = 3.14159265358979323846;
const TAU:   f32 = 6.28318530717958647692;
const E:     f32 = 2.71828182845904523536;
const PHI:   f32 = 1.61803398874989484820;
const SQRT2: f32 = 1.41421356237309504880;
)";

} // namespace vivid::gpu
