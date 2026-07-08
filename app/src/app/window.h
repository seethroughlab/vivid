#pragma once
#include "ui/layout.h"        // vivid::ui::Rect / DockGeom + window-relative geometry
#include "audio/vst3_host.h"  // vivid::session::kMaxTracks (per-track array sizing)

struct GLFWwindow;
struct Vst3PluginWindow;
namespace vivid {
struct App;
class EditorWindow;   // UI-5: floated operator-editor window (app/editor_window.h)
namespace ui { class Renderer2D; class ClipEditor; }
}

namespace vivid {

// A right-click context menu of a track's audio characteristics (the bridge). `graph` marks the
// fx picker as opened from the audio-graph deep view: native effects only, added via the graph
// edit API (audio_graph_add_op → authoritative) rather than the linear device chain.
struct CtxMenu { bool open = false; float x = 0, y = 0; int src = -1; bool graph = false; };  // src: -1 master, >=0 track
// A right-click context menu on a visuals op node (open its source / clone it).
struct NodeMenu { bool open = false; float x = 0, y = 0; int node = -1; bool has_source = false; bool cloneable = false; };

// The one deep view the detail region is showing (ADR-0013, UI-1). Explicit focus is the
// single source of truth for that region — recomputed once per frame — replacing the old
// implicit race where the draw path and the input path each independently re-derived the mode
// from the current selection. `domain` drives the region's header tint (strict-zones principle).
struct FocusContext {
    // AudioGraph (UI-3): the drilled-in per-track audio node graph deep view (audio peer of
    // VisualNode). Set by the dock "Graph" toggle; `track` is the track being viewed.
    // OpEditor (UI-4b): the drilled-in custom editor an operator exports (vivid_draw_editor),
    // hosted in the detail region. Set by the visual-node "Editor" button; `node` is the op.
    enum class Kind { Device, VisualNode, ClipEditor, AudioGraph, OpEditor };
    enum class Dom  { Audio, Visual };
    Kind kind  = Kind::Device;
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
    // UI-2 (ADR-0013): the visuals graph is a deep view, not a permanent pane. By default the
    // right column is the always-on OUTPUT canvas (filling the column); toggling this reveals
    // the node graph below the output (the drill-in "edit its graph" view).
    bool  show_graph = false;
    // UI-3: drilled into the selected track's audio node graph (the detail region shows the
    // per-track audio graph deep view instead of the device chain). Toggled by the dock "Graph"
    // button; persists across frames (the focus recompute reads it).
    bool  show_audio_graph = false;
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
    CtxMenu menu, fx_menu, map_menu, track_menu;   // track_menu = "+ Track" (File is a native OS menu)
    NodeMenu node_menu;                            // right-click on a visuals op node
    int     map_param = -1;
    int     sel_track = 0, sel_device = 0;
    // UI-3: the audio-graph node selected for inline param editing, by stable NODE ID (>= 0);
    // kNoAudioNode = none. (Stage 2 moved this from chain index to node id so params work on any
    // node of a rewired/non-linear graph.) Plus the param knob index being dragged.
    static constexpr int kNoAudioNode = -100;
    int     sel_audio_node = kNoAudioNode;
    int     ag_param_drag  = -1;            // param index being dragged (-1 = none)
    float   ag_param_v0    = 0.f; double ag_param_y0 = 0.0;
    // UI-3 Stage 2: dragging a wire out of a node's output port to rewire the audio graph.
    // ag_wire_from = the source node id (-1 = not dragging); release over an input port connects.
    int     ag_wire_from   = -1;
    double  cur_x = 0, cur_y = 0;   // latest cursor pos (updated each frame; for ghost-wire draw)
    // UI-3 Stage 2 (2i): the audio-graph view transform (on top of the auto-fit). zoom 1 + pan 0 =
    // the fitted view; scroll zooms around the cursor, dragging empty space pans, double-click resets.
    float   ag_zoom = 1.f, ag_pan_x = 0.f, ag_pan_y = 0.f;
    bool    ag_panning = false; double ag_pan_mx0 = 0, ag_pan_my0 = 0; float ag_pan_ox0 = 0, ag_pan_oy0 = 0;
    double  ag_last_click_t = -1;   // for double-click-to-reset the audio-graph view
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
    float   plugin_scroll = 0.f;               // PLUGINS list scroll offset (px)
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

    // Window-relative geometry — each window computes its own from its metrics.
    float        dock_top()        const { return ui::dock_top(win_h, dock_h); }
    // The OUTPUT panel fills the whole visual zone (down to the dock) when the graph is
    // hidden (its deep-view default), or the fixed top band when the graph is revealed below.
    ui::Rect     output_panel()     const {
        ui::Rect p = ui::output_panel(win_w, split_x);
        if (!show_graph) p.h = ui::dock_top(win_h, dock_h) - ui::kPaneMargin - p.y;
        return p;
    }
    ui::Rect     viewer_rect()      const {
        const ui::Rect p = output_panel();
        return { p.x + ui::kPanePad, p.y + ui::kPanelHdH + ui::kPanePad,
                 p.w - 2.f * ui::kPanePad, p.h - ui::kPanelHdH - 2.f * ui::kPanePad };
    }
    ui::Rect     signal_panel()     const { return ui::signal_panel(win_w, win_h, split_x, dock_h); }
    ui::Rect     splitter_rect()    const { return ui::splitter_rect(win_h, dock_h, split_x); }
    ui::Rect     dock_resize_rect() const { return ui::dock_resize_rect(win_w, win_h, dock_h); }
    ui::DockGeom dock_geom()        const { return ui::dock_geom(win_w, win_h, dock_h); }
    ui::DockGeom dock_geom_node()   const { return ui::dock_geom_node(win_w, win_h, dock_h); }
    ui::Rect     dock_chip(int i)   const { return ui::dock_chip(i, win_h, dock_h); }
    ui::Rect     dock_chip_x(int i) const { return ui::dock_chip_x(i, win_h, dock_h); }
};

}  // namespace vivid
