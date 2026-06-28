#pragma once

namespace vivid {

// The named, wireable float uniforms the shader op exposes as input ports. The
// shader node draws one input port per entry; any audio characteristic can be
// wired to any uniform (the P11 bridge: a wire per uniform, not one u_reactive).
// Order/count here must match the `U { ... }` block in the fragment shader and
// the `Uniforms` struct in shader_op.cpp.
inline constexpr int kNumShaderUniforms = 4;
inline constexpr const char* kShaderUniformNames[kNumShaderUniforms] = {
    "warp", "hue", "density", "glow"
};

}  // namespace vivid
