#pragma once
#include <algorithm>          // std::min / std::max (preview placement)
#include <string>
#include "ui/chooser.h"       // the shared Tab palette (the audio graph's lives here)
#include "ui/layout.h"        // vivid::ui::Rect / DockGeom + window-relative geometry
#include "app/output_preview.h" // the floating output-preview panel (ADR-0025 pressure-point #2)
#include "audio/vst3_host.h"  // vivid::session::kMaxTracks (per-track array sizing)
#include "app/runtime_health.h" // ADR-0019: HealthSnapshot cached per-frame (drives the status dot + panel)
#include "ui/toasts.h"        // ADR-0019: transient failure notifications
#include <vector>

struct GLFWwindow;
struct Vst3PluginWindow;
namespace vivid {
struct App;
class EditorWindow;   // UI-5: floated operator-editor window (app/editor_window.h)
namespace ui { class Renderer2D; class ClipEditor; }
}

namespace vivid {

// A right-click context menu of a track's audio characteristics (the bridge).
// src: -1 = master, >= 0 = track.
struct CtxMenu { bool open = false; float x = 0, y = 0; int src = -1; };

// ADR-0022: the modulation shape editor — a floating popover for one control edge (`from` -> the
// param `param` of node `node`). Opened by clicking a wired (magenta) param port; edits the edge's
// amount / curve / bipolar / invert, or removes it. Geometry is shared by draw + input via the
// mod_editor_* helpers below so the hit-rects match what's drawn.
struct ModEditor { bool open = false; float x = 0, y = 0; int node = -1; int param = -1; int from = -1; };
namespace ui {
constexpr float kModEdW = 184.f, kModEdRowH = 26.f, kModEdHdr = 24.f;
inline Rect mod_editor_panel(const ModEditor& m) { return { m.x, m.y, kModEdW, kModEdHdr + 5.f * kModEdRowH + 6.f }; }
inline Rect mod_editor_row(const ModEditor& m, int row) {   // row 0 = amount, 1 = curve, 2 = bipolar, 3 = invert, 4 = remove
    return { m.x + 8.f, m.y + kModEdHdr + row * kModEdRowH + 3.f, kModEdW - 16.f, kModEdRowH - 6.f };
}
inline Rect mod_editor_widget(const ModEditor& m, int row) {   // the control column (right of the label)
    const Rect r = mod_editor_row(m, row);
    return { r.x + 66.f, r.y, r.w - 66.f, r.h };
}
}  // namespace ui
// A right-click context menu on a visuals op node. ADR-0020: one contextual edit action per node —
// Fork&edit a shipped (read-only) shader, Open the editable source of a user shader / cloned C++ op,
// or Clone&edit a compiled built-in. `target` is the shader op-type to fork (ForkEdit) or the file
// path to open (OpenSource).
struct NodeMenu {
    enum class Action { None, OpenSource, ForkEdit, CloneEdit };
    bool open = false; float x = 0, y = 0; int node = -1;
    Action action = Action::None;
    std::string target;
};

// The one deep view the detail region is showing (ADR-0013, UI-1). Explicit focus is the
// single source of truth for that region — recomputed once per frame — replacing the old
// implicit race where the draw path and the input path each independently re-derived the mode
// from the current selection. `domain` drives the region's header tint (strict-zones principle).
struct FocusContext {
    // AudioGraph (UI-3): the drilled-in per-track audio node graph deep view (audio peer of
    // VisualNode). Set by the dock "Graph" toggle; `track` is the track being viewed.
    // OpEditor (UI-4b): the drilled-in custom editor an operator exports (vivid_draw_editor),
    // hosted in the detail region. Set by the visual-node "Editor" button; `node` is the op.
    enum class Kind { VisualNode, ClipEditor, AudioGraph, OpEditor };   // (Device: the linear chain, retired)
    enum class Dom  { Audio, Visual };
    Kind kind  = Kind::AudioGraph;   // a track's default detail view is its audio node graph
    Dom  dom   = Dom::Audio;
    int  track = 0;     // Device / ClipEditor / AudioGraph
    int  scene = -1;    // ClipEditor
    int  node  = -1;    // VisualNode / OpEditor (op index)
};

// Per-window view + interaction state. Many Windows can point at one App, each
// with its own surface, layout, selection, and drag state. The GLFW user pointer
// points at a Window; handlers reach shared state through win->app.
struct Window {
    App*            app    = nullptr;   // shared engine/document (not owned)
    GLFWwindow*     glfw   = nullptr;
    ui::Renderer2D* ui     = nullptr;   // this window's 2D renderer (not owned)
    ui::ClipEditor* editor = nullptr;   // this window's clip editor (not owned)

    // Layout metrics: logical (point) size drives UI layout; framebuffer (physical)
    // size drives the surface. dpi bridges them (2.0 on retina).
    int   win_w = 1280, win_h = 800;
    int   fb_w  = 1280, fb_h  = 800;
    float dpi = 1.0f;
    float split_x = 512.f;   // DAW|visuals splitter x
    float dock_h  = 210.f;   // bottom device-view dock height
    // V1 (ADR-0014): the visuals GRAPH is the persistent right column, and the rendered output floats
    // over it as a movable/resizable preview panel. Its state + geometry live in `OutputPreview` (its
    // own persistent view owner, ADR-0025 pressure-point #2); `preview.show` mirrors the Output node's
    // `preview` param, `preview.out_aspect` is cached from VisualGraph::rt_aspect() each frame.
    OutputPreview preview;
    // UI-3: drilled into the selected track's audio node graph (the detail region shows the
    // per-track audio graph deep view instead of the device chain). Toggled by the dock "Graph"
    // button; persists across frames (the focus recompute reads it).
    // UI-4b: drilled into the selected visual node's operator-exported custom editor (the detail
    // region hosts vivid_draw_editor). Set by the visual-node "Editor" button; the focus recompute
    // only honors it while the selected op actually exports an editor.
    bool  show_op_editor = false;
    // Latest left-mouse-button state (set on GLFW press/release). The OpEditor draws every frame
    // reading this + cur_x/cur_y, so a drag-based operator editor works without an event queue.
    bool  mouse_left_down = false;
    // UI-5: float-out. The "Float" button sets want_float_node to the visual op index; the frame
    // loop opens editor_win for it next tick (window creation deferred out of the input callback).
    int   want_float_node = -1;
    EditorWindow* editor_win = nullptr;   // the floated operator-editor window (null = none)

    // Musical typing (M6.2): the computer keyboard plays the armed track's instrument.
    // Toggle with `. typing_held[slot] holds pitch+1 currently sounding (0 = none) so a
    // key's note-off matches its note-on even if the octave changed mid-hold.
    bool    typing = false;
    int     typing_oct = 0;                   // octave shift; base C = 60 + 12*oct
    float   typing_vel = 0.8f;                // note-on velocity (c/v adjust)
    int     typing_held[16] = {};             // per-semitone-slot sounding pitch+1

    // Interaction / selection (view-local).
    bool    show_mappings = false;            // P28 mapping-overview overlay (toggle: M)
    bool    show_shader_library = false;      // ADR-0021/P1 shader library view (toggle: L)
    bool    show_diagnostics = false;         // ADR-0019 diagnostics panel (toggle: health dot / H)
    bool    show_log = false;                 // ADR-0019 in-app log view (toggle: J)
    bool    show_presets = false;             // ADR-0021/P4 node-preset popover
    int     presets_node = -1;                // the node the preset popover targets
    HealthSnapshot health;                    // ADR-0019: refreshed once per frame; read by the dot + panel
    std::vector<ui::Toast> toasts;            // ADR-0019: live transient notifications (bottom-right)
    uint64_t last_toast_id = 0;               // highest log id already turned into a toast (gate)
    // ADR-0026: the Gemini-key entry modal (Eval ▸ Set Gemini Key…) + an in-flight "Evaluate Output"
    // job whose verdict becomes a toast when it lands. The key itself lives in the Keychain, not here;
    // `gemini_key_buf` is only the transient text being typed. music_eval_job = -1 means none pending.
    bool        show_gemini_key = false;
    std::string gemini_key_buf;
    int         music_eval_job  = -1;
    CtxMenu menu, map_menu;   // the characteristics menu + the bridge map-source picker
    NodeMenu node_menu;                            // right-click on a visuals op node
    ModEditor mod_editor;                          // ADR-0022: the modulation shape editor popover
    int     map_param = -1;
    int     sel_track = 0, sel_device = 0;
    // UI-3: the audio-graph node selected for inline param editing, by stable NODE ID (>= 0);
    // kNoAudioNode = none. (Stage 2 moved this from chain index to node id so params work on any
    // node of a rewired/non-linear graph.) Plus the param knob index being dragged.
    static constexpr int kNoAudioNode = -100;
    int     sel_audio_node = kNoAudioNode;
    int     mod_ed_drag    = -1;   // ADR-0022: the mod-editor slider being dragged (0 amount / 1 curve / -1 none)
    double  cur_x = 0, cur_y = 0;   // latest cursor pos (updated each frame; for ghost-wire draw)
    // The audio-graph view transform AND the in-flight gesture state (param/key/wire/node/pan drags,
    // double-click timers) now live on the persistent AudioNodeGraph instance (ADR-0023 step 6b/6c),
    // which is becoming the stateful interaction owner. The Window no longer carries any ag_* field.
    FocusContext focus;   // what the detail region is showing (recomputed each frame; UI-1)
    int     param_drag = -1; bool param_is_node = false;
    bool    param_drag_horiz = false;   // node slider = horizontal; knob/device = vertical
    bool    param_xy = false;           // UI-4a: XY-pad drag (param_drag = the group's first param; sets it + the next)
    float   param_drag_v0 = 0.f; double param_drag_y0 = 0.0;
    bool    dock_drag = false;
    double  last_dev_t = -1; int last_dev_i = -1;
    int     gain_drag = -1;
    bool    split_drag = false; double split_last_t = -1.0;
    float   sidebar_w = 0.f;   // left browser column width (0 = collapsed); shifts the DAW pane
    GLFWwindow* popout = nullptr;   // pop-out visuals window (fullscreen/large view); nullptr = closed
    int     popout_fb_w = 0, popout_fb_h = 0;   // its framebuffer size (drives the 2nd surface)
    int     popout_display = 0;     // the `display` target it was opened on (reopen if it changes)
    float   plugin_scroll = 0.f;               // PLUGINS list scroll offset (px)
    // A3: the audio graph's Tab chooser — the ONE way to add an audio node (native op, VST3 or
    // CLAP, from the unified catalog). Lives here because AudioNodeGraph is rebuilt each frame,
    // so it can't hold state. The visuals graph's chooser lives on NodeGraph (which persists).
    ui::Chooser audio_chooser;
    // "+ Track" opens the same chooser filtered to instruments; the pick creates the track first.
    bool audio_chooser_new_track = false;
    // Phase 2c: the curated inspector's "+ Add param" picker — the same palette (type-to-filter +
    // scroll) as Tab-to-add, its entries = the selected plugin node's UNPINNED params. On confirm it
    // pins the chosen param. `param_chooser_node` is which node it curates.
    ui::Chooser param_chooser;
    int  param_chooser_node = -1;
    // The same palette also serves as a real enum dropdown (pick a choice) — action selects what
    // confirming does. 0 = add a param (entry tag = param index); 1 = set an enum param
    // (`param_chooser_param`) to the chosen choice (entry tag = choice index).
    int  param_chooser_action = 0;
    int  param_chooser_param  = -1;
    double  last_plugin_t = -1; int last_plugin_i = -1;   // plugin-row double-click tracking
    // Drag a plugin from the browser onto a track (effect) or the +Track slot (instrument).
    int     plugin_drag_i = -1; bool plugin_dragging = false;
    double  plugin_drag_x0 = 0.0, plugin_drag_y0 = 0.0;
    Vst3PluginWindow* track_win[vivid::session::kMaxTracks] = {};  // open instrument editor windows, per track
    Vst3PluginWindow* fx_win[vivid::session::kMaxTracks] = {};     // open effect editor windows (pool)
    double  last_clip_t = -1; int last_clip_track = -1, last_clip_scene = -1;
    // Clip drag/drop in the session grid: source cell + press pos; drags past a
    // threshold, then a drop on another cell moves (or Option = copies) the clip.
    int     clip_drag_t = -1, clip_drag_sc = -1; bool clip_dragging = false;
    int     clip_drag_from_pool = -1;   // >=0 = the drag source is a clip-pool item (sidebar)
    double  clip_drag_x0 = 0.0, clip_drag_y0 = 0.0;   // press pos (screen coords)

    // Frame-side display smoothing (per window).
    float react = 0.f, trHold = 0.f;          // smoothed master level / held transient
    float trkReact[vivid::session::kMaxTracks] = {0}, trkTrHold[vivid::session::kMaxTracks] = {0};
    float trkNoteHold[vivid::session::kMaxTracks] = {0};   // decayed note-on flash (per-track note.gate source)

    // Window-relative geometry — each window computes its own from its metrics.
    float        dock_top()        const { return ui::dock_top(win_h, dock_h); }
    // ADR-0014: the graph owns the visuals column; the output preview floats over it. The preview's
    // own geometry + clamp live on `OutputPreview` (ADR-0025); call `preview.clamp(visuals_panel())`.
    ui::Rect     visuals_panel()    const { return ui::visuals_panel(win_w, win_h, split_x, dock_h); }
    ui::Rect     splitter_rect()    const { return ui::splitter_rect(win_h, dock_h, split_x); }
    ui::Rect     dock_resize_rect() const { return ui::dock_resize_rect(win_w, win_h, dock_h); }
    ui::DockGeom dock_geom()        const { return ui::dock_geom(win_w, win_h, dock_h); }
    ui::DockGeom dock_geom_node()   const { return ui::dock_geom_node(win_w, win_h, dock_h); }
};

}  // namespace vivid
