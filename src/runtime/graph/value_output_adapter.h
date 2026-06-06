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

} // namespace vivid
