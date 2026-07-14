#pragma once
#include <algorithm>          // std::min / std::max (preview placement)
#include <string>
#include "ui/chooser.h"       // the shared Tab palette (the audio graph's lives here)
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

// A right-click context menu of a track's audio characteristics (the bridge).
// src: -1 = master, >= 0 = track.
struct CtxMenu { bool open = false; float x = 0, y = 0; int src = -1; };
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
    // V1 (ADR-0014): the visuals GRAPH is the persistent right column, and the rendered output
    // floats over it as a movable/resizable preview panel. `preview_show` mirrors the Output
    // node's `preview` param (V4); the geometry is view state. preview_x < 0 = "not placed yet"
    // (the frame loop parks it in the column's bottom-right on the first frame it draws).
    bool  preview_show = true;
    float preview_x = -1.f, preview_y = -1.f, preview_w = 420.f;
    bool  preview_drag = false, preview_resize = false;
    double preview_grab_x = 0, preview_grab_y = 0;   // cursor->panel offset while dragging
    // The live output aspect (w/h), cached from VisualGraph::rt_aspect() each frame so the pure
    // geometry helpers (and the input hit-tests) can derive the preview's height without this
    // header having to know about the GPU layer. Derived state — never a param slot.
    float out_aspect = 16.f / 9.f;
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
    CtxMenu menu, map_menu;   // the characteristics menu + the bridge map-source picker
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
    // Dragging a source node's key-range handle (a key-split): 0 = lo, 1 = hi, -1 = none.
    int     ag_key_drag    = -1;
    int     ag_key_v0      = 0; double ag_key_y0 = 0.0;
    // UI-3 Stage 2: dragging a wire out of a node's output port to rewire the audio graph.
    // ag_wire_from = the source node id (-1 = not dragging); release over an input port connects.
    int     ag_wire_from   = -1;
    // Dragging an audio-graph node's body to reposition it: the node id (-1 = none) + the grab
    // offset in world units (cursor-to-node-origin), so the node follows the cursor under zoom.
    int     ag_node_drag   = -1; float ag_node_dx = 0.f, ag_node_dy = 0.f;
    double  cur_x = 0, cur_y = 0;   // latest cursor pos (updated each frame; for ghost-wire draw)
    // UI-3 Stage 2 (2i): the audio-graph view transform (on top of the auto-fit). zoom 1 + pan 0 =
    // the fitted view; scroll zooms around the cursor, dragging empty space pans, double-click resets.
    float   ag_zoom = 1.f, ag_pan_x = 0.f, ag_pan_y = 0.f;
    bool    ag_panning = false; double ag_pan_mx0 = 0, ag_pan_my0 = 0; float ag_pan_ox0 = 0, ag_pan_oy0 = 0;
    double  ag_last_click_t = -1;   // for double-click-to-reset the audio-graph view
    int     ag_last_node = -1; double ag_last_node_t = -1;   // double-click a node → open its plugin editor
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
    // ADR-0014: the graph owns the visuals column; the output preview floats over it.
    ui::Rect     visuals_panel()    const { return ui::visuals_panel(win_w, win_h, split_x, dock_h); }
    ui::Rect     preview_panel()    const { return ui::preview_panel(preview_x, preview_y, preview_w, out_aspect); }
    ui::Rect     viewer_rect()      const { return ui::preview_viewer_rect(preview_x, preview_y, preview_w, out_aspect); }
    ui::Rect     preview_header()   const { return ui::preview_header_rect(preview_x, preview_y, preview_w); }
    ui::Rect     preview_close()    const { return ui::preview_close_rect(preview_x, preview_y, preview_w); }
    ui::Rect     preview_grip()     const { return ui::preview_grip_rect(preview_x, preview_y, preview_w, out_aspect); }
    // Keep the preview inside the visuals column. MUST run every frame, not just on placement: the
    // panel's height is derived from the output aspect, so switching the Output node to (say) 9:16
    // makes a fixed-width preview much TALLER. Left unbounded that pushed the blit rect outside the
    // framebuffer, and wgpu aborts the process on an out-of-bounds scissor.
    void clamp_preview() {
        const ui::Rect g = visuals_panel();
        if (g.w <= 0.f || g.h <= 0.f) return;
        const float max_w = std::max(ui::kPreviewMinW, g.w - 2.f * ui::kPanePad);
        const float max_h = std::max(ui::kPanelHdH + 40.f, g.h - 2.f * ui::kPanePad);
        preview_w = std::clamp(preview_w, ui::kPreviewMinW, std::min(ui::kPreviewMaxW, max_w));
        // Height follows the aspect — if that overflows the column, give width back until it fits.
        if (ui::kPanelHdH + ui::preview_body_h(preview_w, out_aspect) > max_h)
            preview_w = std::max(ui::kPreviewMinW, (max_h - ui::kPanelHdH) * out_aspect);
        const ui::Rect p = ui::preview_panel(0.f, 0.f, preview_w, out_aspect);
        if (preview_x < 0.f) {   // never placed: park it bottom-right of the column
            preview_x = g.x + g.w - p.w - ui::kPanePad;
            preview_y = g.y + g.h - p.h - ui::kPanePad;
        }
        preview_x = std::clamp(preview_x, g.x, std::max(g.x, g.x + g.w - p.w));
        preview_y = std::clamp(preview_y, g.y, std::max(g.y, g.y + g.h - p.h));
    }
    ui::Rect     splitter_rect()    const { return ui::splitter_rect(win_h, dock_h, split_x); }
    ui::Rect     dock_resize_rect() const { return ui::dock_resize_rect(win_w, win_h, dock_h); }
    ui::DockGeom dock_geom()        const { return ui::dock_geom(win_w, win_h, dock_h); }
    ui::DockGeom dock_geom_node()   const { return ui::dock_geom_node(win_w, win_h, dock_h); }
};

}  // namespace vivid
