#pragma once

#include <cstdint>

namespace vivid {

// Runtime execution rate of a node.
enum class Cadence : uint8_t {
    Frame = 0,   // ~60 Hz, main thread (control + GPU)
    Audio = 1,   // ~48 kHz, audio thread
};

} // namespace vivid
