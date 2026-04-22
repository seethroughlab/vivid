#include "runtime/debug/output_window.h"
#include "runtime/gpu/gpu_context.h"
#include "common/gpu_util.h"
#include <glfw3webgpu.h>
#include <cstdio>

#ifdef __APPLE__
#include "runtime/platform/macos_menu.h"
#endif

namespace vivid {

static void key_callback(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        auto* self = static_cast<OutputWindow*>(glfwGetWindowUserPointer(w));
        if (self) {
            // Set close_requested_ via should_close() — we use the window user pointer
            // to signal, but we need a way to set the private member.  Use the window's
            // own should-close flag which we poll in should_close().
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        }
    }
}

bool OutputWindow::open(WGPUInstance instance, WGPUAdapter adapter,
                         WGPUDevice device, WGPUQueue queue,
                         GLFWmonitor* monitor) {
    if (window_) return true; // already open

    if (!monitor) monitor = glfwGetPrimaryMonitor();
    if (!monitor) return false;

    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) return false;

    int mx = 0, my = 0;
    glfwGetMonitorPos(monitor, &mx, &my);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);

    window_ = glfwCreateWindow(mode->width, mode->height, "Vivid Output", nullptr, nullptr);
    if (!window_) {
        std::fprintf(stderr, "[vivid] OutputWindow: failed to create GLFW window\n");
        return false;
    }

    glfwSetWindowPos(window_, mx, my);
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, key_callback);
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

    device_ = device;
    queue_ = queue;

    surface_ = glfwCreateWindowWGPUSurface(instance, window_);
    if (!surface_) {
        std::fprintf(stderr, "[vivid] OutputWindow: failed to create surface\n");
        glfwDestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    // Query surface format
    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(surface_, adapter, &caps);
    if (caps.formatCount > 0) {
        surface_format_ = caps.formats[0];
    } else {
        surface_format_ = WGPUTextureFormat_BGRA8Unorm;
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    // Configure surface
    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);
    width_ = static_cast<uint32_t>(fb_w > 0 ? fb_w : mode->width);
    height_ = static_cast<uint32_t>(fb_h > 0 ? fb_h : mode->height);
    configure_surface(width_, height_);

    // Init blit pipeline
    if (!blit_.init(device_, surface_format_)) {
        std::fprintf(stderr, "[vivid] OutputWindow: failed to init blit pipeline\n");
        wgpuSurfaceUnconfigure(surface_);
        wgpuSurfaceRelease(surface_);
        surface_ = nullptr;
        glfwDestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    close_requested_ = false;

#ifdef __APPLE__
    macos_set_presentation_fullscreen(true);
#endif

    std::fprintf(stderr, "[vivid] OutputWindow opened (%ux%u)\n", width_, height_);
    return true;
}

void OutputWindow::close() {
    if (!window_) return;

    blit_.shutdown();

    if (surface_) {
        wgpuSurfaceUnconfigure(surface_);
        wgpuSurfaceRelease(surface_);
        surface_ = nullptr;
    }

    glfwDestroyWindow(window_);
    window_ = nullptr;
    device_ = nullptr;
    queue_ = nullptr;
    surface_format_ = WGPUTextureFormat_Undefined;
    width_ = 0;
    height_ = 0;
    close_requested_ = false;

#ifdef __APPLE__
    macos_set_presentation_fullscreen(false);
#endif

    std::fprintf(stderr, "[vivid] OutputWindow closed\n");
}

bool OutputWindow::is_open() const {
    return window_ != nullptr;
}

bool OutputWindow::should_close() const {
    if (!window_) return false;
    return close_requested_ || glfwWindowShouldClose(window_);
}

bool OutputWindow::move_to_monitor(GLFWmonitor* monitor) {
    if (!window_ || !monitor) return false;
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) return false;

    int mx = 0, my = 0;
    glfwGetMonitorPos(monitor, &mx, &my);
    glfwSetWindowPos(window_, mx, my);
    glfwSetWindowSize(window_, mode->width, mode->height);
    // Surface will be reconfigured on next present() when size change is detected
    std::fprintf(stderr, "[vivid] OutputWindow moved to monitor (%dx%d at %d,%d)\n",
                 mode->width, mode->height, mx, my);
    return true;
}

bool OutputWindow::present(WGPUTextureView source_tex,
                            uint32_t src_w, uint32_t src_h, FitMode fit_mode) {
    if (!window_ || !surface_) return false;

    // Check for framebuffer resize
    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);
    if (fb_w <= 0 || fb_h <= 0) return false;

    uint32_t new_w = static_cast<uint32_t>(fb_w);
    uint32_t new_h = static_cast<uint32_t>(fb_h);
    if (new_w != width_ || new_h != height_) {
        wgpuSurfaceUnconfigure(surface_);
        width_ = new_w;
        height_ = new_h;
        configure_surface(width_, height_);
        std::fprintf(stderr, "[vivid] OutputWindow resized (%ux%u)\n", width_, height_);
    }

    // Acquire surface texture
    WGPUSurfaceTexture st{};
    wgpuSurfaceGetCurrentTexture(surface_, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        if (st.texture) wgpuTextureRelease(st.texture);
        return false;
    }

    WGPUTextureViewDescriptor view_desc{};
    view_desc.label = to_sv("Output Surface View");
    view_desc.format = surface_format_;
    view_desc.dimension = WGPUTextureViewDimension_2D;
    view_desc.baseMipLevel = 0;
    view_desc.mipLevelCount = 1;
    view_desc.baseArrayLayer = 0;
    view_desc.arrayLayerCount = 1;
    WGPUTextureView dest = wgpuTextureCreateView(st.texture, &view_desc);
    if (!dest) {
        wgpuTextureRelease(st.texture);
        return false;
    }

    // Create command encoder
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = to_sv("Output Window Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &enc_desc);

    blit_.blit_fit(encoder, source_tex, dest,
                   src_w, src_h, width_, height_,
                   fit_mode, /*ui_visible=*/false);

    gpu_submit(device_, queue_, encoder, "Output Window Commands");

    wgpuSurfacePresent(surface_);
    wgpuTextureViewRelease(dest);
    wgpuTextureRelease(st.texture);
    return true;
}

void OutputWindow::configure_surface(uint32_t w, uint32_t h) {
    WGPUSurfaceConfiguration config{};
    config.device = device_;
    config.format = surface_format_;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.width = w;
    config.height = h;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(surface_, &config);
}

} // namespace vivid
