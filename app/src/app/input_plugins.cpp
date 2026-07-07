#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window.h"
#include "ui/layout.h"
#include "ui/node_graph.h"          // NodeGraph::select_op / on_up
#include "audio/vst3_host.h"
#include "audio/plugin_catalog.h"   // plugin_at / plugin_count

#include <algorithm>
#include <string>

namespace {
using namespace vivid::ui;   // hit, track_* rects, kTrackW/kHeaderY/kTopBarH, plugin_row_at, plugins_scroll_max
namespace S = vivid::session;

// Double-click a browser plugin row to add it (auto-route): an instrument becomes a new track,
// anything else an effect on the selected track.
void add_plugin(vivid::App& app, vivid::Window& win, int idx) {
    auto* s = app.session;
    if (!s) return;
    const std::string path = S::plugin_at(idx).path;
    if (path.empty()) return;
    const int t = S::session_add_instrument_track(s, path.c_str());
    if (t >= 0) { win.sel_track = t; win.sel_device = 0; if (app.graph) app.graph->select_op(-1); return; }
    const int tracks = S::session_track_count(s);
    if (tracks <= 0) return;
    const int seltr = std::min(std::max(win.sel_track, 0), tracks - 1);
    S::session_add_effect(s, seltr, path.c_str());   // not an instrument -> effect on the selected track
    if (app.graph) app.graph->select_op(-1);
}

// The plugin-drop target under (mx,my): a track index for an effect, -2 for the "+Track" slot
// (new instrument), or -1 for nothing. dmx is DAW-pane x (mx - sidebar).
int plugin_drop_target(const vivid::Window& win, int tracks, double dmx, double my) {
    if (tracks < S::kMaxTracks && hit(track_add_rect(tracks), dmx, my)) return -2;   // +Track slot
    for (int t = 0; t < tracks; ++t)                     // a track header or its clip column
        if (hit(track_header_rect(t), dmx, my) ||
            (dmx >= track_x(t) && dmx < track_x(t) + kTrackW && my >= kHeaderY && my < win.dock_top()))
            return t;
    if (my >= win.dock_top()) return std::min(std::max(win.sel_track, 0), tracks - 1);   // the dock = selected track
    return -1;
}

// Drop a browsed plugin onto a track (effect) or the +Track slot (new instrument).
void drop_plugin(vivid::App& app, vivid::Window& win, int idx, double mx, double my) {
    auto* s = app.session;
    if (!s) return;
    const std::string path = S::plugin_at(idx).path;
    if (path.empty()) return;
    const int tracks = S::session_track_count(s);
    const int tgt = plugin_drop_target(win, tracks, mx - win.sidebar_w, my);
    if (tgt == -1) return;
    if (tgt == -2) {   // new instrument track
        const int t = S::session_add_instrument_track(s, path.c_str());
        if (t >= 0) { win.sel_track = t; win.sel_device = 0; }
    } else {           // effect on the dropped-on track
        S::session_add_effect(s, tgt, path.c_str());
        win.sel_track = tgt;
    }
    if (app.graph) app.graph->select_op(-1);
}
}  // namespace

namespace vivid::input {

// Scroll over the sidebar's PLUGINS panel scrolls the plugin list. Returns true when consumed.
bool plugins_scroll(Window& win, App& app, double yoff, double mx, double my) {
    (void)app;
    if (win.sidebar_w > 0.f && mx < win.sidebar_w && my >= kTopBarH && my < win.dock_top()) {
        const float smax = plugins_scroll_max(win.sidebar_w, win.win_h, win.dock_h, S::plugin_count());
        win.plugin_scroll = std::min(smax, std::max(0.f, win.plugin_scroll - static_cast<float>(yoff) * 26.f));
        return true;
    }
    return false;
}

// On mouse release, drop the dragged plugin (if any) onto a track / +Track slot. Returns true
// when a plugin drag was in progress (consumed the release).
bool plugins_release(Window& win, App& app, double mx, double my) {
    if (win.plugin_drag_i >= 0) {
        if (win.plugin_dragging) drop_plugin(app, win, win.plugin_drag_i, mx, my);
        win.plugin_drag_i = -1; win.plugin_dragging = false;
        if (app.graph) app.graph->on_up(mx, my);
        return true;
    }
    return false;
}

// PLUGINS panel press (inside the sidebar guard): double-click a row to add (auto-route), or arm
// a drag of a row onto a track (effect) / the +Track slot (instrument).
void plugins_sidebar_press(Window& win, App& app, double mx, double my) {
    const int np = S::plugin_count();
    const int pr = plugin_row_at(win.sidebar_w, win.win_h, win.dock_h, win.plugin_scroll, np, mx, my);
    if (pr >= 0) {
        const double now = glfwGetTime();
        if (win.last_plugin_i == pr && now - win.last_plugin_t < 0.35) {
            add_plugin(app, win, pr); win.last_plugin_t = -1; win.plugin_drag_i = -1;   // consumed by the double-click
        } else {
            win.last_plugin_i = pr; win.last_plugin_t = now;
            win.plugin_drag_i = pr; win.plugin_dragging = false;   // arm a potential drag-to-track
            win.plugin_drag_x0 = mx; win.plugin_drag_y0 = my;
        }
    }
}

}  // namespace vivid::input
