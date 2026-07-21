#pragma once
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "app/log.h"               // ADR-0019 (E4): the leveled logger (owned here)
#include "app/project_state.h"
#include "gpu/op_runtime.h"        // OpRegistry (operator-based visuals)
#include "gpu/operator_loader.h"   // OperatorLoader (dlopen'd operators; owned here)
#include "gpu/shader_library.h"    // ShaderLibrary (ADR-0016: a shader FILE is an operator)
#include "gpu/file_drop_registry.h" // FileDropRegistry (ADR-0021/P3: drop a file -> the op for it)
#include "packages/hot_reload_manager.h"   // live operator hot-reload (opt-in/dev)
#include "platform/midi_input.h"           // hardware MIDI input (M6.4)
#include "audio/music_eval.h"              // ADR-0026: in-app Gemini music evaluation

namespace vivid {
class GpuContext;
class VisualGraph;
class ControlServer;
class TextureSource;
class EditGateway;
class CrashRecovery;
namespace ui { class NodeGraph; class AudioNodeGraph; }
}
namespace vivid::session { struct Session; }
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
    ui::AudioNodeGraph* audio_graph = nullptr; // audio node editor (ADR-0023 step 6: one persistent instance)
    vivid::session::Session* session   = nullptr;   // audio session (or null -> test tone)
    Transport*          transport = nullptr;   // master clock
    ControlServer*      control   = nullptr;   // MCP loopback server
    EditGateway*        edit_gateway = nullptr; // ADR-0017 undo/redo command sink (a main.cpp local)
    CrashRecovery*      crash_recovery = nullptr; // ADR-0018 warm-snapshot writer (a main.cpp local)
    TextureSource*      srcTex    = nullptr;   // shared visuals source texture
    OpRegistry          op_registry;           // built-in + loaded operators
    // Loaders for dlopen'd operator dylibs. Owned here so each outlives the
    // registry factory that captures its raw pointer (App lives the whole run).
    std::vector<std::unique_ptr<OperatorLoader>> op_loaders;
    // The shader library (ADR-0016). Owns the parsed ShaderDefs, which every shader node —
    // and every cached descriptor built from one — points into, so it lives the whole run.
    ShaderLibrary shader_library;
    // Which operators accept which dropped file extensions (ADR-0021/P3). Rebuilt from op_loaders
    // after the startup scan and after each live package install.
    FileDropRegistry file_drops;
    HotReloadManager hot_reload;   // watches operator sources + live-swaps (opt-in)
    platform::MidiInput midi_in;   // hardware MIDI input; drained each frame to the armed track (M6.4)
    Logger              log;       // ADR-0019 leveled logger; drained each frame (drain_rt)
    MusicEval           music_eval; // ADR-0026: in-app Gemini audio evaluation (async jobs)

    int visual_source = 0;   // 0 = plasma shader, 1 = video (mirrors vgraph generator)
    bool recovered_unsaved = false;   // ADR-0018: a launch-time autosave recovery ran; mark dirty post-baseline
    std::set<std::string> quarantined_ops;   // ADR-0018: ops disabled this launch (repeat crashers / safe mode)

    // Minimal project workflow (UI/main thread only). The session JSON remains the
    // document format; these fields remember where it lives and where relative media starts.
    ProjectState project;
    void remember_project_path(const std::string& path);
    void set_media_root(const std::string& root);

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
    double click_phase = 0.0;                // metronome click oscillator (M6.3)
    float  click_amp = 0.f;                  // click envelope (decays each sample)
    float  click_freq = 1000.f;              // downbeat vs. beat pitch
    long long click_last_beat = -1;          // last integer beat that fired a click
};

}  // namespace vivid
