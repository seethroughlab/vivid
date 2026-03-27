#pragma once

#include <cstdint>

namespace vivid {

// Runtime execution rate of a node.
enum class Cadence : uint8_t {
    Frame = 0,   // ~60 Hz, main thread (control + GPU)
    Audio = 1,   // ~48 kHz, audio thread
};

// Per-node cadence override (stored in NodeDef, serialized as integer 0/1/2/3).
enum class CadenceOverride : uint8_t {
    Auto          = 0,  // runtime decides (default); inference may promote to audio
    Frame         = 1,  // force frame-rate execution
    Audio         = 2,  // force audio-rate execution (requires audio-capable operator)
    InferredAudio = 3,  // compiler-assigned; treated as Audio on subsequent builds
};

} // namespace vivid
