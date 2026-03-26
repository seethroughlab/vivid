#pragma once

#include <cstdint>

namespace vivid {

// Per-node cadence override (stored in NodeDef, serialized as integer 0/1/2).
enum class CadenceOverride : uint8_t {
    Auto  = 0,  // runtime decides (default)
    Frame = 1,  // force frame-rate execution
    Audio = 2,  // force audio-rate execution (requires audio-capable operator)
};

} // namespace vivid
