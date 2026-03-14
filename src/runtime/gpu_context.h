#pragma once

#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <cstdint>
#include <string>

namespace vivid {

struct FrameState {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
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

    WGPUInstance instance() const { return instance_; }
    WGPUAdapter adapter() const { return adapter_; }
    WGPUDevice device() const { return device_; }
    WGPUQueue queue() const { return queue_; }
    WGPUTextureFormat surface_format() const { return surface_format_; }
    bool surface_supports_copy_src() const { return surface_copy_src_; }
    bool bc_texture_compression_enabled() const { return bc_texture_compression_enabled_; }
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
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    // Last error captured from the uncaptured error callback (for crash diagnostics)
    std::string last_error_;
    WGPUErrorType last_error_type_ = WGPUErrorType_NoError;
};

} // namespace vivid
