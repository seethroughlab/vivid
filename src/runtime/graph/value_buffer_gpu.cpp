#include "runtime/graph/value_buffer_gpu.h"
#include "operator_api/gpu_common.h"
#include <webgpu/webgpu.h>

namespace vivid {

// ---------------------------------------------------------------------------
// ValueBuffer::release_gpu — release WebGPU storage buffer if present.
// Defined here (not in value_buffer.h) to avoid pulling webgpu.h into
// audio-path headers.
// ---------------------------------------------------------------------------

void ValueBuffer::release_gpu() {
    if (gpu_buffer) {
        wgpuBufferRelease(gpu_buffer);
        gpu_buffer = nullptr;
    }
    gpu_backing = ValueGpuBacking::CpuOnly;
    gpu_buffer_capacity = 0;
}

// ---------------------------------------------------------------------------
// value_buffer_ensure_gpu — lazy CPU→GPU upload with caching.
//
// Frame-thread only. Creates or resizes the storage buffer if needed,
// uploads CPU data when CpuDirty. No-op when already Synced with matching
// length. Multiple GPU consumers of the same ValueBuffer share the cached
// upload within a single frame.
// ---------------------------------------------------------------------------

void value_buffer_ensure_gpu(ValueBuffer* buf, WGPUDevice device, WGPUQueue queue) {
    if (!buf || buf->committed_count == 0) return;

    // Already synced with sufficient capacity — cache hit.
    if (buf->gpu_backing == ValueGpuBacking::Synced &&
        buf->gpu_buffer_capacity >= buf->committed_count) {
        return;
    }

    // Create or resize GPU buffer if needed.
    if (!buf->gpu_buffer || buf->gpu_buffer_capacity < buf->committed_count) {
        if (buf->gpu_buffer) wgpuBufferRelease(buf->gpu_buffer);
        buf->gpu_buffer = gpu::create_storage_buffer(
            device, buf->committed_count * sizeof(float), "value_buffer");
        buf->gpu_buffer_capacity = buf->committed_count;
    }

    // Upload CPU data.
    wgpuQueueWriteBuffer(queue, buf->gpu_buffer, 0,
                         buf->floats.data(),
                         buf->committed_count * sizeof(float));
    buf->gpu_backing = ValueGpuBacking::Synced;
}

} // namespace vivid
