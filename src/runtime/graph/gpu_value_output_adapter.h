#pragma once

#include "operator_api/types.h"  // VividValueOutput
#include <webgpu/webgpu.h>       // WGPUTextureView

namespace vivid {

// ---------------------------------------------------------------------------
// Texture value-output adapter (lane-value clean-break, Phase 4c).
//
// A GPU operator's texture output is the value-model analogue of the CPU buffer
// builders: the runtime PROVIDES the render target, so resize() returns the
// pre-allocated output texture view (the op renders into it) and commit() is a
// no-op (the texture is already bound for the command encoder). The handle IS
// the WGPUTextureView.
// ---------------------------------------------------------------------------

inline void* texture_value_output_resize_fn(void* handle, uint32_t /*count*/) {
    return handle;  // the runtime-provided WGPUTextureView, as void*
}

inline void texture_value_output_commit_fn(void* /*handle*/, uint32_t /*count*/) {
    // no-op: the output texture is already the render target
}

inline VividValueOutput make_texture_value_output(WGPUTextureView view) {
    VividValueOutput out{};
    out.handle = static_cast<void*>(view);
    out.resize = texture_value_output_resize_fn;
    out.commit = texture_value_output_commit_fn;
    out.set_string = nullptr;
    return out;
}

} // namespace vivid
