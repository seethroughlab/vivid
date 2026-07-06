#pragma once
namespace vivid {
class OpRegistry;
// Register the built-in native audio operators (AO-1 spikes + AO-3 glitch effects)
// into the shared registry, alongside the built-in visual ops.
void register_builtin_audio_ops(OpRegistry& reg);
}
