#pragma once

namespace vivid {

// The named, wireable float ports the visuals node exposes; any audio
// characteristic can be wired to any one (the bridge: a wire per port).
// Routing (see main.cpp): ports 0-3 drive the plasma generator's uniforms;
// ports 4-5 drive the FBO effect chain (feedback decay, blur radius).
inline constexpr int kNumShaderUniforms = 6;
inline constexpr const char* kShaderUniformNames[kNumShaderUniforms] = {
    "warp", "hue", "density", "glow", "feedback", "blur"
};

}  // namespace vivid
