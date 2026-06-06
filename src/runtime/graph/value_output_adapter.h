#pragma once

#include "operator_api/types.h"
#include "lane_buffer.h"

namespace vivid {

// ---------------------------------------------------------------------------
// Trampoline bridging VividValueOutput (lane-value clean-break, Phase 4) to a
// runtime-owned buffer. In Phase 4a the FLOAT value output is backed by the same
// LaneBuffer the lane path uses (out_lane_bufs[p]) — so a value-API operator's
// output flows downstream through the unchanged lane propagation, and lane-API
// and value-API operators interoperate freely. String/custom value outputs and
// native ValueArena-backed transport come in later increments.
//
// All callbacks are allocation-free on a fixed (audio) buffer (LaneBuffer::resize
// returns nullptr rather than allocating) — RT-safe.
// ---------------------------------------------------------------------------

inline void* value_output_resize_fn(void* handle, uint32_t count) {
    return static_cast<LaneBuffer*>(handle)->resize(count);  // float* as void*
}

inline void value_output_commit_fn(void* handle, uint32_t count) {
    static_cast<LaneBuffer*>(handle)->commit(count);
}

// Make a float VividValueOutput backed by a LaneBuffer (the lane transport).
// set_string is null in 4a (float payload only).
inline VividValueOutput make_value_output(LaneBuffer* buf) {
    VividValueOutput out{};
    out.handle = buf;
    out.resize = value_output_resize_fn;
    out.commit = value_output_commit_fn;
    out.set_string = nullptr;
    return out;
}

// ---- String value output (Phase 4b) ----
// The many-string VividValueOutput is backed by the same StringLaneBuffer the
// string-lane path uses (out_string_lane_bufs[p]) — so a value-API string
// operator's output flows downstream through the unchanged string-lane
// propagation, and string-lane + value-API operators interoperate. resize()
// returns the handle (non-null sentinel) on success, null on overflow (no alloc).

inline void* string_value_output_resize_fn(void* handle, uint32_t count) {
    return static_cast<StringLaneBuffer*>(handle)->resize(count) ? handle : nullptr;
}

inline void string_value_output_commit_fn(void* handle, uint32_t count) {
    static_cast<StringLaneBuffer*>(handle)->commit(count);
}

inline void string_value_output_set_string_fn(void* handle, uint32_t index, const char* value) {
    static_cast<StringLaneBuffer*>(handle)->set(index, value);
}

inline VividValueOutput make_string_value_output(StringLaneBuffer* buf) {
    VividValueOutput out{};
    out.handle = buf;
    out.resize = string_value_output_resize_fn;
    out.commit = string_value_output_commit_fn;
    out.set_string = string_value_output_set_string_fn;
    return out;
}

} // namespace vivid
