#pragma once
namespace vivid {
class OpRegistry;
// Register the built-in native audio operators (spikes + glitch effects)
// into the shared registry, alongside the built-in visual ops.
void register_builtin_audio_ops(OpRegistry& reg);
}
