#pragma once

#include <cstdint>

namespace vivid {

// ---------------------------------------------------------------------------
// Lane behavior — how an operator interacts with lane multiplicity.
// ---------------------------------------------------------------------------

enum class LaneBehavior : uint8_t {
    Pointwise   = 0,  // processes each lane independently, preserves lane set
    Structural  = 1,  // creates, reshapes, reorders, or filters lanes
    Reduction   = 2,  // collapses many lanes into fewer (often one)
    Kernel      = 3,  // needs cross-lane access (neighborhood / full collection)
};

// ---------------------------------------------------------------------------
// LaneSet — the multiplicity descriptor for a value flowing through an edge.
// ---------------------------------------------------------------------------

struct LaneSet {
    uint32_t lane_set_id     = 0;      // 0 = scalar (one lane, no provenance)
    uint32_t lane_count      = 1;      // 1 = scalar
    bool     identity_bearing = false;  // true for voice-like sets with persistent per-lane state

    bool is_scalar() const { return lane_set_id == 0 && lane_count <= 1; }
};

// ---------------------------------------------------------------------------
// LaneExecutionStrategy — how the runtime evaluates lanes for a given node.
// Selected by the compiler/planner, not by operator authors.
// ---------------------------------------------------------------------------

enum class LaneExecutionStrategy : uint8_t {
    Scalar          = 0,  // lane_count=1, single instance, no lifting
    InstancePerLane = 1,  // N instances (LaneLiftGroup), deinterleave/interleave
    LoopBased       = 2,  // single instance, runtime-driven loop over lanes
    // Future: GpuCompute = 3
};

} // namespace vivid
