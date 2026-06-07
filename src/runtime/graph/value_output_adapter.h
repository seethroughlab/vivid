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


// ---- Native value-buffer string output (Phase 7d.4) ----
// String output backed by a ValueBuffer(STRING) — the value-substrate successor to the
// StringLaneBuffer-backed overload above.
inline void* string_valuebuf_output_resize_fn(void* handle, uint32_t count) {
    return static_cast<ValueBuffer*>(handle)->ensure(count) ? handle : nullptr;
}
inline void string_valuebuf_output_commit_fn(void* handle, uint32_t count) {
    static_cast<ValueBuffer*>(handle)->commit(count);
}
inline void string_valuebuf_output_set_string_fn(void* handle, uint32_t index, const char* value) {
    static_cast<ValueBuffer*>(handle)->set_string(index, value);
}
inline VividValueOutput make_string_value_output(ValueBuffer* buf) {
    VividValueOutput out{};
    out.handle = buf;
    out.resize = string_valuebuf_output_resize_fn;
    out.commit = string_valuebuf_output_commit_fn;
    out.set_string = string_valuebuf_output_set_string_fn;
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
