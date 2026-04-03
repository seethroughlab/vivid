#pragma once

#include "runtime/gpu/fullscreen_blit.h"
#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <cstdint>

namespace vivid {

class OutputWindow {
public:
    bool open(WGPUInstance instance, WGPUAdapter adapter,
              WGPUDevice device, WGPUQueue queue,
              GLFWmonitor* monitor);
    void close();
    bool is_open() const;
    bool move_to_monitor(GLFWmonitor* monitor);
    bool present(WGPUTextureView source_tex,
                 uint32_t src_w, uint32_t src_h, FitMode fit_mode);
    bool should_close() const;

private:
    GLFWwindow* window_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUSurface surface_ = nullptr;
    WGPUTextureFormat surface_format_ = WGPUTextureFormat_Undefined;
    uint32_t width_ = 0, height_ = 0;
    FullscreenBlit blit_;
    bool close_requested_ = false;

    void configure_surface(uint32_t w, uint32_t h);
};

} // namespace vivid
