#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window.h"
#include "ui/layout.h"
#include "ui/node_graph.h"          // NodeGraph::select_op / on_up
#include "audio/vst3_host.h"
#include "audio/plugin_catalog.h"   // plugin_at / plugin_count
#include "ui/plugin_browser.h"      // plugin_browser_rows — the same filtered list the draw uses

#include <algorithm>
#include <string>
#include <vector>

namespace {
using namespace vivid::ui;   // hit, track_* rects, kTrackW/kHeaderY/kTopBarH, plugin_row_at, plugins_scroll_max
namespace S = vivid::session;

// Double-click a browser plugin row to add it (auto-route): an instrument becomes a new track,
// anything else an effect on the selected track.
void add_plugin(vivid::App& app, vivid::Window& win, int idx) {
    auto* s = app.session;
    if (!s) return;
    const S::PluginInfo& pi = S::plugin_at(idx);
    const std::string path = pi.path;
    if (path.empty()) return;
    if (pi.format == S::kFmtCLAP) {   // CLAP loads async via the request API (not the VST3 loader)
        const int t = S::session_add_graph_track(s, "");   // a bare track for the CLAP instrument
        if (t >= 0) { S::session_request_track_clap_instrument(s, t, path.c_str());
                      win.sel_track = t; win.sel_device = 0; }
        if (app.graph) app.graph->select_op(-1);
        return;
    }
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
    const S::PluginInfo& pi = S::plugin_at(idx);
    const std::string path = pi.path;
    if (path.empty()) return;
    const int tracks = S::session_track_count(s);
    const int tgt = plugin_drop_target(win, tracks, mx - win.sidebar_w, my);
    if (tgt == -1) return;
    const bool is_clap = (pi.format == S::kFmtCLAP);   // CLAP loads async via the request API
    if (tgt == -2) {   // new instrument track
        const int t = is_clap ? S::session_add_graph_track(s, "")
                              : S::session_add_instrument_track(s, path.c_str());
        if (t >= 0) { if (is_clap) S::session_request_track_clap_instrument(s, t, path.c_str());
                      win.sel_track = t; win.sel_device = 0; }
    } else {           // effect on the dropped-on track
        if (is_clap) S::session_request_track_clap_effect(s, tgt, path.c_str());
        else         S::session_add_effect(s, tgt, path.c_str());
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
        // Clamp against the FILTERED row count — otherwise a search result of 2 rows still scrolls
        // as if all 30 were there, into blank space.
        const int shown = static_cast<int>(plugin_browser_rows(win.plugin_filter).size());
        const float smax = plugins_scroll_max(win.sidebar_w, win.win_h, win.dock_h, shown);
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

// PLUGINS panel press (inside the sidebar guard): focus the search field, double-click a row to
// add (auto-route), or arm a drag of a row onto a track (effect) / the +Track slot (instrument).
// Rows are the FILTERED list, so an index is resolved back to a catalog index through the same
// helper the draw uses (ui/plugin_browser.h) — the two can never disagree.
void plugins_sidebar_press(Window& win, App& app, double mx, double my) {
    if (hit(plugins_search_rect(win.sidebar_w, win.win_h, win.dock_h), mx, my)) {
        win.plugin_search_focus = true;   // typed characters now go to the filter
        return;
    }
    win.plugin_search_focus = false;      // a click anywhere else in the panel drops the field
    const std::vector<int> rows = plugin_browser_rows(win.plugin_filter);
    const int pr = plugin_row_at(win.sidebar_w, win.win_h, win.dock_h, win.plugin_scroll,
                                 static_cast<int>(rows.size()), mx, my);
    if (pr < 0) return;
    const int cat = rows[static_cast<std::size_t>(pr)];   // filtered row -> catalog index
    const double now = glfwGetTime();
    if (win.last_plugin_i == cat && now - win.last_plugin_t < 0.35) {
        add_plugin(app, win, cat); win.last_plugin_t = -1; win.plugin_drag_i = -1;   // consumed by the double-click
    } else {
        win.last_plugin_i = cat; win.last_plugin_t = now;
        win.plugin_drag_i = cat; win.plugin_dragging = false;   // arm a potential drag-to-track
        win.plugin_drag_x0 = mx; win.plugin_drag_y0 = my;
    }
}

// The PLUGINS search field owns the keyboard while focused. Returns true when the key/char was
// consumed (so it doesn't fall through to the global shortcuts — otherwise typing "m" in the
// search box would toggle the mapping overlay).
bool plugins_search_key(Window& win, int key) {
    if (!win.plugin_search_focus) return false;
    if (key == GLFW_KEY_ESCAPE) { win.plugin_filter.clear(); win.plugin_search_focus = false; return true; }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) { win.plugin_search_focus = false; return true; }
    if (key == GLFW_KEY_BACKSPACE) {
        if (!win.plugin_filter.empty()) win.plugin_filter.pop_back();
        win.plugin_scroll = 0.f;
        return true;
    }
    return true;   // swallow every other key while the field has focus
}

bool plugins_search_char(Window& win, unsigned int cp) {
    if (!win.plugin_search_focus) return false;
    if (cp >= 32 && cp < 127) { win.plugin_filter.push_back(static_cast<char>(cp)); win.plugin_scroll = 0.f; }
    return true;
}

}  // namespace vivid::input
