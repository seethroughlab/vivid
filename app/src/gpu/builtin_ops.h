#pragma once

namespace vivid {
class OpRegistry;

// Register the built-in visuals operators (Plasma, Video, Feedback, Blur, Output)
// into `reg`. Each wraps the existing GLSL ShaderOp/EffectOp behind the lifted
// operator ABI. Call once at App init.
void register_builtin_ops(OpRegistry& reg);

}  // namespace vivid
