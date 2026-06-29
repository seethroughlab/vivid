#pragma once

// Shared application state for the PoC shell, split out of main.cpp so the audio
// callback, the draw modules, and the input handlers can all see it. Every member
// is a pointer or a scalar, so forward declarations keep this header light.
struct Transport;
struct Vst3PluginWindow;
namespace vivid_poc { struct Session; }
namespace vivid { namespace ui { class NodeGraph; class ClipEditor; } }

// A right-click context menu of a track's audio characteristics (the bridge).
struct CtxMenu { bool open = false; float x = 0, y = 0; int src = -1; };  // src: -1 master, >=0 track

struct AudioState {
    Transport* transport = nullptr;
    vivid_poc::Session* session = nullptr;  // hosted instrument + clips (or null -> test tone)
    vivid::ui::NodeGraph* graph = nullptr;  // visuals node editor (UI thread)
    CtxMenu menu;                           // characteristic picker (UI thread)
    CtxMenu fx_menu;                        // "+ FX" picker (src = track)
    CtxMenu map_menu;                       // param "map from source" picker
    int map_param = -1;                     // param index the map menu targets
    int sel_track = 0;                      // track whose device chain is shown
    int sel_device = 0;                     // device whose params are shown (0=inst, 1+=fx)
    int param_drag = -1;                    // device/node param (knob) being dragged
    bool param_is_node = false;             // knob targets the selected visual node's base
    float param_drag_v0 = 0.f;              // value + cursor-y at knob grab (vertical drag)
    double param_drag_y0 = 0.0;
    bool dock_drag = false;                 // dragging the device-dock resize handle
    double last_dev_t = -1; int last_dev_i = -1;  // device double-click detect
    int gain_drag = -1;                     // mixer gain slider being dragged (UI thread)
    bool split_drag = false;                // dragging the DAW|visuals splitter
    double split_last_t = -1.0;             // last splitter press (double-click reset)
    Vst3PluginWindow* track_win[8] = {};    // open instrument editor windows, per track
    Vst3PluginWindow* fx_win[8] = {};       // open effect editor windows (pool)
    vivid::ui::ClipEditor* editor = nullptr;       // MIDI piano-roll (UI thread)
    double last_clip_t = -1; int last_clip_track = -1, last_clip_scene = -1;  // double-click detect
    float tr_baseline = 0.f;                // onset detector baseline (audio thread)
    float m_flt_lo = 0.f, m_flt_hi = 0.f;   // master 3-band crossover states (audio thread)
    double phase = 0.0;       // test-tone oscillator phase
    double tone_hz = 110.0;   // low A
};
