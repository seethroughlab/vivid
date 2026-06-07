#pragma once

#include "operator_api/types.h"
#include "lane_buffer.h"
#include "value_buffer.h"

namespace vivid {

// ---- Float lane output backed by a ValueBuffer (Phase 7 shim) ----
// Lets the still-live lane-API operators (test fixture ops) write into the
// native value transport (out_value_bufs) during the 7a transition, so a
// lane-API op's output flows downstream through value propagation. Removed in
// Phase 7d with the lane API.

inline float* lane_output_from_value_resize_fn(void* handle, uint32_t length) {
    auto* b = static_cast<ValueBuffer*>(handle);
    return b->ensure(length) ? b->floats_ptr() : nullptr;
}

inline void lane_output_from_value_commit_fn(void* handle, uint32_t length) {
    static_cast<ValueBuffer*>(handle)->commit(length);
}

inline VividLaneOutput make_lane_output(ValueBuffer* buf) {
    VividLaneOutput out{};
    out.handle = buf;
    out.resize = lane_output_from_value_resize_fn;
    out.commit = lane_output_from_value_commit_fn;
    return out;
}

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

// ---- String lane output backed by a ValueBuffer(STRING) (Phase 7d.4 shim) ----
// Lets the still-live string lane-API fixture ops write into the value-substrate string
// output (out_string_value_bufs). Removed in 7d.5 with the lane API.
inline uint8_t string_lane_output_from_value_resize_fn(void* handle, uint32_t length) {
    return static_cast<ValueBuffer*>(handle)->ensure(length) ? 1 : 0;
}
inline void string_lane_output_from_value_set_fn(void* handle, uint32_t index, const char* value) {
    static_cast<ValueBuffer*>(handle)->set_string(index, value);
}
inline void string_lane_output_from_value_commit_fn(void* handle, uint32_t length) {
    static_cast<ValueBuffer*>(handle)->commit(length);
}
inline VividStringLaneOutput make_string_lane_output(ValueBuffer* buf) {
    VividStringLaneOutput out{};
    out.handle = buf;
    out.resize = string_lane_output_from_value_resize_fn;
    out.set = string_lane_output_from_value_set_fn;
    out.commit = string_lane_output_from_value_commit_fn;
    return out;
}

} // namespace vivid
