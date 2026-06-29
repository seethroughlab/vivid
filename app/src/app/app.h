#pragma once
#include <string>
#include <vector>

#include "gpu/op_runtime.h"   // OpRegistry (operator-based visuals)

namespace vivid {
class GpuContext;
class VisualGraph;
class ControlServer;
class TextureSource;
namespace ui { class NodeGraph; }
}
namespace vivid_poc { struct Session; }
struct Transport;
struct VideoPlayer;

namespace vivid {

// Shared engine + document state: ONE per process, referenced by every Window via
// Window::app. The audio thread sees only this (the ma_device user pointer is an
// App*), never a Window — so opening/closing a window never touches audio.
struct App {
    // Engine / document (shared across all windows).
    GpuContext*         gpu       = nullptr;   // owns the wgpu device/queue
    VisualGraph*        vgraph    = nullptr;   // the visuals pipeline (model)
    ui::NodeGraph*      graph     = nullptr;   // node editor + mapping registry (model)
    vivid_poc::Session* session   = nullptr;   // audio session (or null -> test tone)
    Transport*          transport = nullptr;   // master clock
    ControlServer*      control   = nullptr;   // MCP loopback server
    TextureSource*      srcTex    = nullptr;   // shared visuals source texture
    OpRegistry          op_registry;           // built-in + (future) loaded operators

    int visual_source = 0;   // 0 = plasma shader, 1 = video (mirrors vgraph generator)

    // Video playback (UI/main thread only).
    VideoPlayer*             video = nullptr;
    std::vector<std::string> video_paths;
    int                      video_idx = -1;
    void load_video_at(int i);   // open clip i (wraps), play if the source is video

    // Audio-thread DSP state (touched only inside the audio callback).
    float  m_flt_lo = 0.f, m_flt_hi = 0.f;   // master 3-band crossover states
    float  tr_baseline = 0.f;                // onset detector baseline
    double phase = 0.0;                      // test-tone oscillator phase
    double tone_hz = 110.0;
};

}  // namespace vivid
