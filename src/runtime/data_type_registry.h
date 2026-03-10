#pragma once

#include <cstddef>
#include <string_view>

namespace vivid {

// Shared data-type size registry for legacy VIVID_PORT_DATA string-tagged types.
// Note: media_stream_v1 and media_clock_v1 have been promoted to first-class
// port enum values (VIVID_PORT_MEDIA_STREAM, VIVID_PORT_MEDIA_CLOCK) and are
// no longer registered here.
inline size_t data_type_size(std::string_view /*data_type*/) {
    return 0;
}

} // namespace vivid

