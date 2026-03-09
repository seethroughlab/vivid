#pragma once

#include <cstddef>
#include <string_view>

#include "operator_api/media_clock.h"
#include "operator_api/media_stream.h"

namespace vivid {

// Shared data-type metadata used by scheduler/audio bridge and diagnostics.
inline size_t data_type_size(std::string_view data_type) {
    if (data_type == "media_clock_v1") return sizeof(MediaClockV1);
    if (data_type == "media_stream_v1") return sizeof(MediaStreamV1);
    return 0;
}

} // namespace vivid

