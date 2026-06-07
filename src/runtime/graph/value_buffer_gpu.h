#pragma once

#include "runtime/graph/value_buffer.h"
#include <webgpu/webgpu.h>

namespace vivid {

// ---------------------------------------------------------------------------
// GPU value buffer helpers — frame-thread only, NEVER call from audio thread.
// Successor to lane_buffer_gpu (lane-value clean-break, Phase 7).
// ---------------------------------------------------------------------------

// Ensures buf has an up-to-date GPU storage buffer. Creates or resizes the
// buffer if needed, uploads CPU data if CpuDirty. After return, gpu_backing
// is Synced and gpu_buffer is directly bindable as a storage buffer.
void value_buffer_ensure_gpu(ValueBuffer* buf, WGPUDevice device, WGPUQueue queue);

} // namespace vivid
