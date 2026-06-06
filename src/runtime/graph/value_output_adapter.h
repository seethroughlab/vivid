#pragma once

#include "operator_api/types.h"
#include "lane_buffer.h"
#include "value_buffer.h"

namespace vivid {

// ---- Native value-buffer float output (Phase 7) ----
// A float VividValueOutput backed by a ValueBuffer (the native value transport,
// successor to the LaneBuffer-backed overload below). ensure()+floats_ptr() is
// RT-safe: a fixed (audio) buffer returns nullptr rather than allocating.

inline void* value_buffer_output_resize_fn(void* handle, uint32_t count) {
    auto* b = static_cast<ValueBuffer*>(handle);
    return b->ensure(count) ? static_cast<void*>(b->floats_ptr()) : nullptr;
}

inline void value_buffer_output_commit_fn(void* handle, uint32_t count) {
    static_cast<ValueBuffer*>(handle)->commit(count);
}

inline VividValueOutput make_value_output(ValueBuffer* buf) {
    VividValueOutput out{};
    out.handle = buf;
    out.resize = value_buffer_output_resize_fn;
    out.commit = value_buffer_output_commit_fn;
    out.set_string = nullptr;
    return out;
}

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

// ---- Audio value output (Phase 5a) ----
// An audio op writes into the runtime-PROVIDED output block (output_buffers[port],
// fixed buffer_size). Like the texture output, resize() returns that block (the op
// writes buffer_size*channels samples into it) and commit() is a no-op. The handle
// IS the float* audio block.

inline void* audio_value_output_resize_fn(void* handle, uint32_t /*count*/) {
    return handle;  // the runtime-provided audio block, as void*
}

inline void audio_value_output_commit_fn(void* /*handle*/, uint32_t /*count*/) {
    // no-op: the output block is the audio transport buffer
}

inline VividValueOutput make_audio_value_output(float* buf) {
    VividValueOutput out{};
    out.handle = static_cast<void*>(buf);
    out.resize = audio_value_output_resize_fn;
    out.commit = audio_value_output_commit_fn;
    out.set_string = nullptr;
    return out;
}

} // namespace vivid
