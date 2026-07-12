#pragma once
// Animated startup splash — ported from vivid-classic (src/runtime/core/main.cpp).
// A dark nebula shader with the node-graph "V" logo (the same mark as the app icon)
// drawn over it, plus a centered info panel (title / version / copyright / status).
// Rendered during the blocking startup phases (operator scan + VST3 instrument load)
// so the window shows a branded frame instead of nothing while the engine boots.
//
// The shader pass draws into the frame's 4x MSAA color target (matching every other
// frame pipeline); the panel text is overlaid via Renderer2D. Owns its GPU objects.
#include <webgpu/webgpu.h>
#include <cstdint>

struct GLFWwindow;

namespace vivid {
class GpuContext;
namespace ui { class Renderer2D; }

class Splash {
public:
    Splash() = default;
    ~Splash() { shutdown(); }
    Splash(const Splash&) = delete;
    Splash& operator=(const Splash&) = delete;

    // Build the nebula+logo pipeline. `surface_format` + `msaa` must match the frame
    // target (GpuContext renders the whole frame into a 4x MSAA view). Returns false
    // (and stays inert) if the shader/pipeline fails — render() then no-ops gracefully.
    bool init(WGPUDevice device, WGPUTextureFormat surface_format, uint32_t msaa);

    // Draw one animated splash frame: nebula+logo shader, then the info panel + `status`
    // line via `ui`. Acquires + presents its own frame and pumps events, so it is safe
    // to call from a blocking load's progress callback. No-op if init() failed.
    void render(GpuContext& gpu, ui::Renderer2D& ui, GLFWwindow* window, const char* status);

    void shutdown();

private:
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup      bind_group_ = nullptr;
    WGPUBuffer         uniform_buf_ = nullptr;
    double             start_time_ = 0.0;   // seconds since first render (drives the animation)
    float              seed_ = 0.0f;        // per-launch nebula variation
};

}  // namespace vivid
