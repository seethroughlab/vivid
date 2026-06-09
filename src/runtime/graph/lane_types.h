#pragma once

#include <cstdint>
#include "operator_api/value_model.h"  // VividValueType/Multiplicity/IdentityMode/StorageKind

namespace vivid {

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
// edge/port. Computed by the value-flow pass (Pass 2.7) and the SOLE multiplicity
// authority. multiplicity carries one-vs-many, identity carries how "many" is
// named, value_type/storage are first-class, and provenance_group_id groups
// same-origin many-values for UI coloring.
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
