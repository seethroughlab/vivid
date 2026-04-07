#pragma once

#include "runtime/graph/lane_buffer.h"
#include <webgpu/webgpu.h>

namespace vivid {

// ---------------------------------------------------------------------------
// GPU lane buffer helpers — frame-thread only, NEVER call from audio thread.
// ---------------------------------------------------------------------------

// Ensures buf has an up-to-date GPU storage buffer. Creates or resizes the
// buffer if needed, uploads CPU data if CpuDirty. After return, gpu_backing
// is Synced and gpu_buffer is directly bindable as a storage buffer.
void lane_buffer_ensure_gpu(LaneBuffer* buf, WGPUDevice device, WGPUQueue queue);

} // namespace vivid
