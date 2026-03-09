#pragma once

#include <cstdint>

#include "operator_api/media_clock.h"

namespace vivid {

// Opaque media stream contract shared across domains.
// The handle identifies a shared session object owned outside the operator graph.
// The embedded clock snapshot is authoritative for consumers that need timing.
struct MediaStreamV1 {
    uint64_t handle_id = 0;
    uint64_t session_ptr = 0; // Direct same-process pointer to media session payload.
    uint64_t source_generation = 0;
    uint32_t schema_version = 1;
    uint32_t flags = 0;
    MediaClockV1 clock{};
};

} // namespace vivid
