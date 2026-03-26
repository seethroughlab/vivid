#pragma once

#include <cstdint>

#include "operator_api/media_clock.h"
#include "operator_api/type_id.h"

namespace vivid {

// Opaque media stream contract shared across execution environments.
// The handle identifies a shared session object owned outside the operator graph.
// The embedded clock snapshot is authoritative for consumers that need timing.
struct MediaStreamV1 {
    uint64_t handle_id = 0;
    uint64_t source_generation = 0;
    uint32_t schema_version = 1;
    uint32_t flags = 0;
    MediaClockV1 clock{};
};

} // namespace vivid

VIVID_DECLARE_CUSTOM_REF_TYPE(vivid::MediaStreamV1,
                              "seethroughlab.vivid.media_stream_v1",
                              "MediaStreamV1",
                              true);
