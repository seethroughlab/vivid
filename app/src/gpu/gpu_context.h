#pragma once

#include <webgpu/webgpu.h>
#include <cstdint>
#include <string>
#include "gpu/gpu_util.h"   // kMsaaSamples, to_sv

struct GLFWwindow;

namespace vivid {

struct FrameState {
    WGPUTexture texture = nullptr;          // swap-chain surface texture (present target)
    WGPUTextureView view = nullptr;         // what the app renders into (4x MSAA color)
    WGPUTextureView resolve_view = nullptr; // surface view; MSAA resolves here in end_frame
    WGPUCommandEncoder encoder = nullptr;
};

class GpuContext {
public:
    GpuContext() = default;
    ~GpuContext();

    // Non-copyable
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    bool init(GLFWwindow* window, uint32_t width, uint32_t height);
    void resize(uint32_t width, uint32_t height);
    bool begin_frame(FrameState& frame);
    bool end_frame(const FrameState& frame);
    void discard_frame(const FrameState& frame);
    void shutdown();

    uint32_t sample_count() const { return kMsaaSamples; }

    WGPUInstance instance() const { return instance_; }
    WGPUAdapter adapter() const { return adapter_; }
    WGPUDevice device() const { return device_; }
    WGPUQueue queue() const { return queue_; }
    WGPUTextureFormat surface_format() const { return surface_format_; }
    bool surface_supports_copy_src() const { return surface_copy_src_; }
    bool bc_texture_compression_enabled() const { return bc_texture_compression_enabled_; }
    bool device_lost() const { return device_lost_; }
    const std::string& last_error() const { return last_error_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

private:
    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUSurface surface_ = nullptr;
    WGPUTextureFormat surface_format_ = WGPUTextureFormat_Undefined;
    bool surface_copy_src_ = false;
    bool bc_texture_compression_enabled_ = false;
    bool device_lost_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    // 4x MSAA color target the whole frame renders into; resolved to the surface
    // in end_frame. Recreated on resize. (void ensure_msaa below.)
    WGPUTexture msaa_tex_ = nullptr;
    WGPUTextureView msaa_view_ = nullptr;
    uint32_t msaa_w_ = 0;
    uint32_t msaa_h_ = 0;
    void ensure_msaa(uint32_t width, uint32_t height);

    // Last error captured from the uncaptured error callback (for crash diagnostics)
    std::string last_error_;
    WGPUErrorType last_error_type_ = WGPUErrorType_NoError;
};

// Finish, submit, and release a command encoder.  Returns false if the encoder
// was in an error state (null command buffer).  GPU errors are handled by the
// uncaptured error callback configured on the device — error scopes are not
// used because wgpu-native panics if PushErrorScope is called on a lost device.
bool gpu_submit(WGPUDevice device, WGPUQueue queue, WGPUCommandEncoder encoder,
                const char* label = "Commands");

} // namespace vivid
