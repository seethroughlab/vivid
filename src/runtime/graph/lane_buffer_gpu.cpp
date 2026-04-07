#include "runtime/graph/lane_buffer_gpu.h"
#include "operator_api/gpu_common.h"
#include <webgpu/webgpu.h>

namespace vivid {

// ---------------------------------------------------------------------------
// LaneBuffer::release_gpu — release WebGPU storage buffer if present.
// Defined here (not in lane_buffer.h) to avoid pulling webgpu.h into
// audio-path headers.
// ---------------------------------------------------------------------------

void LaneBuffer::release_gpu() {
    if (gpu_buffer) {
        wgpuBufferRelease(gpu_buffer);
        gpu_buffer = nullptr;
    }
    gpu_backing = LaneGpuBacking::CpuOnly;
    gpu_buffer_capacity = 0;
}

// ---------------------------------------------------------------------------
// lane_buffer_ensure_gpu — lazy CPU→GPU upload with caching.
//
// Frame-thread only. Creates or resizes the storage buffer if needed,
// uploads CPU data when CpuDirty. No-op when already Synced with matching
// length. Multiple GPU consumers of the same LaneBuffer share the cached
// upload within a single frame.
// ---------------------------------------------------------------------------

void lane_buffer_ensure_gpu(LaneBuffer* buf, WGPUDevice device, WGPUQueue queue) {
    if (!buf || buf->committed_length == 0) return;

    // Already synced with sufficient capacity — cache hit.
    if (buf->gpu_backing == LaneGpuBacking::Synced &&
        buf->gpu_buffer_capacity >= buf->committed_length) {
        return;
    }

    // Create or resize GPU buffer if needed.
    if (!buf->gpu_buffer || buf->gpu_buffer_capacity < buf->committed_length) {
        if (buf->gpu_buffer) wgpuBufferRelease(buf->gpu_buffer);
        buf->gpu_buffer = gpu::create_storage_buffer(
            device, buf->committed_length * sizeof(float), "lane_buffer");
        buf->gpu_buffer_capacity = buf->committed_length;
    }

    // Upload CPU data.
    wgpuQueueWriteBuffer(queue, buf->gpu_buffer, 0,
                         buf->data.data(),
                         buf->committed_length * sizeof(float));
    buf->gpu_backing = LaneGpuBacking::Synced;
}

} // namespace vivid
