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

    bool is_scalar() const { return multiplicity == VIVID_MULTIPLICITY_SCALAR; }
};

// Project a LaneSet into the value-model multiplicity/identity — the equivalence
// target the value-flow pass is proven against. (value_type/storage come from the
// port type + cadence, supplied by the caller.)
inline ValueEnvelope envelope_from_lane_set(const LaneSet& ls, VividValueType vt,
                                            VividStorageKind sk) {
    ValueEnvelope e;
    e.value_type    = vt;
    e.multiplicity  = ls.is_scalar() ? VIVID_MULTIPLICITY_SCALAR : VIVID_MULTIPLICITY_MANY;
    e.value_count   = ls.lane_count;
    e.identity_mode = ls.is_scalar()        ? VIVID_IDENTITY_NONE
                    : (ls.identity_bearing  ? VIVID_IDENTITY_STABLE_IDS
                                            : VIVID_IDENTITY_POSITIONAL);
    e.storage_kind  = sk;
    return e;
}

} // namespace vivid
