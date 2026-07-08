#pragma once

#include <webgpu/webgpu.h>
#include <cstdint>
#include <string>
#include <atomic>
#include "gpu/gpu_util.h"   // kMsaaSamples, to_sv

struct GLFWwindow;

namespace vivid {

struct FrameState {
    WGPUTexture texture = nullptr;          // swap-chain surface texture (present target)
    WGPUTextureView view = nullptr;         // what the app renders into (4x MSAA color)
    WGPUTextureView resolve_view = nullptr; // surface view; MSAA resolves here in end_frame
    WGPUCommandEncoder encoder = nullptr;
};

// One auxiliary swap-chain surface for a secondary OS window (the visuals pop-out, an editor
// float-out, …) plus its own MSAA color target. Shares the primary device/queue/format — the
// methods take those so the struct owns no context. begin/end mirror GpuContext::begin/end_frame.
struct AuxSurface {
    WGPUSurface     surface   = nullptr;
    WGPUTexture     msaa_tex  = nullptr;
    WGPUTextureView msaa_view = nullptr;
    uint32_t w = 0, h = 0, msaa_w = 0, msaa_h = 0;

    bool is_open() const { return surface != nullptr; }
    bool open(WGPUInstance inst, WGPUDevice dev, WGPUTextureFormat fmt, GLFWwindow* win,
              uint32_t width, uint32_t height, const char* label);
    void resize(WGPUDevice dev, WGPUTextureFormat fmt, uint32_t width, uint32_t height);
    void close();
    // Acquire the next surface texture + an MSAA view to render into. False if unavailable.
    bool begin(WGPUDevice dev, WGPUTextureFormat fmt, bool device_lost, FrameState& frame, const char* label);
    // Resolve + present. False (and frees the frame) on submit failure.
    bool end(WGPUDevice dev, WGPUQueue queue, bool device_lost, const FrameState& frame, const char* label);
private:
    void ensure_msaa(WGPUDevice dev, WGPUTextureFormat fmt, uint32_t width, uint32_t height, const char* label);
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

    // Secondary output surface — the pop-out visuals window. Shares this device/queue;
    // the visuals FBO is blitted into it (see VisualGraph::present_to). Thin delegators over
    // aux_popout_ (an AuxSurface). Absent (has_secondary()==false) when closed.
    bool open_secondary(GLFWwindow* window, uint32_t width, uint32_t height);
    void close_secondary();
    void resize_secondary(uint32_t width, uint32_t height);
    bool has_secondary() const { return aux_popout_.is_open(); }
    bool begin_secondary(FrameState& frame);
    bool end_secondary(const FrameState& frame);

    // Editor float-out surface (UI-5) — an independent secondary window for an operator's custom
    // editor, drawn with its own Renderer2D. Same shared device/queue as everything else.
    bool open_editor_surface(GLFWwindow* window, uint32_t width, uint32_t height);
    void close_editor_surface();
    void resize_editor_surface(uint32_t width, uint32_t height);
    bool has_editor_surface() const { return aux_editor_.is_open(); }
    bool begin_editor_surface(FrameState& frame);
    bool end_editor_surface(const FrameState& frame);

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
    uint32_t error_count() const { return error_count_.load(std::memory_order_relaxed); }
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

    // Secondary (pop-out) + editor float-out surfaces. Share device_/queue_/format via AuxSurface.
    AuxSurface aux_popout_;
    AuxSurface aux_editor_;

    // Last error captured from the uncaptured error callback (for crash diagnostics)
    std::string last_error_;
    WGPUErrorType last_error_type_ = WGPUErrorType_NoError;
    std::atomic<uint32_t> error_count_{0};   // total uncaptured errors (health signal, P4.3)
};

// Finish, submit, and release a command encoder.  Returns false if the encoder
// was in an error state (null command buffer).  GPU errors are handled by the
// uncaptured error callback configured on the device — error scopes are not
// used because wgpu-native panics if PushErrorScope is called on a lost device.
bool gpu_submit(WGPUDevice device, WGPUQueue queue, WGPUCommandEncoder encoder,
                const char* label = "Commands");

} // namespace vivid
