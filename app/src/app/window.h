#pragma once
#include "ui/layout.h"        // vivid::ui::Rect / DockGeom + window-relative geometry
#include "audio/vst3_host.h"  // vivid::session::kMaxTracks (per-track array sizing)

struct GLFWwindow;
struct Vst3PluginWindow;
namespace vivid {
struct App;
namespace ui { class Renderer2D; class ClipEditor; }
}

namespace vivid {

// A right-click context menu of a track's audio characteristics (the bridge).
struct CtxMenu { bool open = false; float x = 0, y = 0; int src = -1; };  // src: -1 master, >=0 track
// A right-click context menu on a visuals op node (open its source / clone it).
struct NodeMenu { bool open = false; float x = 0, y = 0; int node = -1; bool has_source = false; };

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

    // Interaction / selection (view-local).
    bool    show_mappings = false;            // P28 mapping-overview overlay (toggle: M)
    CtxMenu menu, fx_menu, map_menu, track_menu;   // track_menu = "+ Track" (File is a native OS menu)
    NodeMenu node_menu;                            // right-click on a visuals op node
    int     map_param = -1;
    int     sel_track = 0, sel_device = 0;
    int     param_drag = -1; bool param_is_node = false;
    bool    param_drag_horiz = false;   // node slider = horizontal; knob/device = vertical
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
    ui::Rect     viewer_rect()      const { return ui::viewer_rect(win_w, split_x); }
    ui::Rect     output_panel()     const { return ui::output_panel(win_w, split_x); }
    ui::Rect     signal_panel()     const { return ui::signal_panel(win_w, win_h, split_x, dock_h); }
    ui::Rect     splitter_rect()    const { return ui::splitter_rect(win_h, dock_h, split_x); }
    ui::Rect     dock_resize_rect() const { return ui::dock_resize_rect(win_w, win_h, dock_h); }
    ui::DockGeom dock_geom()        const { return ui::dock_geom(win_w, win_h, dock_h); }
    ui::DockGeom dock_geom_node()   const { return ui::dock_geom_node(win_w, win_h, dock_h); }
    ui::Rect     dock_chip(int i)   const { return ui::dock_chip(i, win_h, dock_h); }
    ui::Rect     dock_chip_x(int i) const { return ui::dock_chip_x(i, win_h, dock_h); }
};

}  // namespace vivid
