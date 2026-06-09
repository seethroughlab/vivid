#pragma once

#include <cstdint>
#include "operator_api/value_model.h"  // VividValueType/Multiplicity/IdentityMode/StorageKind

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
// LaneExecutionStrategy — how the runtime evaluates lanes for a given node.
// Selected by the compiler/planner, not by operator authors.
// ---------------------------------------------------------------------------

enum class LaneExecutionStrategy : uint8_t {
    Scalar          = 0,  // lane_count=1, single instance, no lifting
    InstancePerLane = 1,  // N instances (LaneLiftGroup), deinterleave/interleave
    LoopBased       = 2,  // single instance, runtime-driven loop over lanes
    // Future: GpuCompute = 3
};

// ---------------------------------------------------------------------------
// ValueEnvelope — the value-model descriptor for a value flowing through an
// edge/port (lane-value clean-break, Phase 2). Computed by the value-flow pass
// in PARALLEL with LaneSet; not yet consumed by execution (Phases 4-5). The
// successor to LaneSet: multiplicity replaces lane_set_id/lane_count, identity
// replaces identity_bearing, and value_type/storage are first-class.
// ---------------------------------------------------------------------------

struct ValueEnvelope {
    VividValueType    value_type    = VIVID_VALUE_FLOAT;
    VividMultiplicity multiplicity  = VIVID_MULTIPLICITY_SCALAR;
    uint32_t          value_count   = 1;                    // mirrors lane_count (runtime-refined)
    VividIdentityMode identity_mode = VIVID_IDENTITY_NONE;
    VividStorageKind  storage_kind  = VIVID_STORAGE_CPU;
    // Provenance group id — which Many values share an origin (for UI wire/port
    // coloring). 0/1 = no distinct provenance. The value-model successor to
    // lane_set_id (lane-value clean-break 7e.5); allocated natively in value-flow.
    uint32_t          provenance_group_id = 0;

    bool is_scalar() const { return multiplicity == VIVID_MULTIPLICITY_SCALAR; }
};

} // namespace vivid
