#pragma once

#include "operator_api/types.h"
#include "lane_buffer.h"

namespace vivid {

// ---------------------------------------------------------------------------
// Trampoline functions bridging VividLaneOutput / VividStringLaneOutput
// callbacks to runtime-owned LaneBuffer / StringLaneBuffer.
//
// All callbacks are allocation-free and audio-thread safe.
// ---------------------------------------------------------------------------

// ---- Float lane output ----

inline float* lane_output_resize_fn(void* handle, uint32_t length) {
    return static_cast<LaneBuffer*>(handle)->resize(length);
}

inline void lane_output_commit_fn(void* handle, uint32_t length) {
    static_cast<LaneBuffer*>(handle)->commit(length);
}

inline VividLaneOutput make_lane_output(LaneBuffer* buf) {
    VividLaneOutput out{};
    out.handle = buf;
    out.resize = lane_output_resize_fn;
    out.commit = lane_output_commit_fn;
    return out;
}

// ---- String lane output ----

inline uint8_t string_lane_output_resize_fn(void* handle, uint32_t length) {
    return static_cast<StringLaneBuffer*>(handle)->resize(length);
}

inline void string_lane_output_set_fn(void* handle, uint32_t index, const char* value) {
    static_cast<StringLaneBuffer*>(handle)->set(index, value);
}

inline void string_lane_output_commit_fn(void* handle, uint32_t length) {
    static_cast<StringLaneBuffer*>(handle)->commit(length);
}

inline VividStringLaneOutput make_string_lane_output(StringLaneBuffer* buf) {
    VividStringLaneOutput out{};
    out.handle = buf;
    out.resize = string_lane_output_resize_fn;
    out.set = string_lane_output_set_fn;
    out.commit = string_lane_output_commit_fn;
    return out;
}

} // namespace vivid
