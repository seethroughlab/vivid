#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window.h"
#include "ui/layout.h"
#include "ui/session_view.h"     // meter_hit
#include "ui/node_graph.h"       // drop_track_sources / select_op
#include "audio/vst3_host.h"
#include "audio/vst3_plugin_window.h"   // vst3_plugin_window_close (track removal shifts indices)

#include <algorithm>
#include <cstdio>

namespace {
using namespace vivid::ui;   // hit/Rect, clip_cell_rect, track_* rects, scene_launch_rect, pool_item_*, in_sidebar, meter_hit
namespace S = vivid::session;

// The clip cell under (mx,my), or false. Fills t/sc on a hit.
bool clip_cell_at(int tracks, int scenes, double mx, double my, int& t, int& sc) {
    for (int a = 0; a < tracks; ++a)
        for (int b = 0; b < scenes; ++b)
            if (hit(clip_cell_rect(a, b), mx, my)) { t = a; sc = b; return true; }
    return false;
}
// Move (or copy) a MIDI clip from (st,ss) to (tt,ts) via the thread-safe clip API. Instrument
// tracks only (audio clips carry a sample and aren't relocatable here).
void move_clip(vivid::App& app, int st, int ss, int tt, int ts, bool copy) {
    auto* s = app.session;
    if (!s || S::session_track_is_audio(s, st) || S::session_track_is_audio(s, tt)) return;
    S::ClipNote buf[512];
    const int n = S::session_get_clip(s, st, ss, buf, 512);
    if (n <= 0) return;   // nothing to move
    const double len = S::session_clip_length(s, st, ss);
    S::session_set_clip(s, tt, ts, buf, n, len);
    if (!copy) S::session_set_clip(s, st, ss, nullptr, 0, len);   // clear the source
}
// Whether a pooled clip can be placed on a track (audio clip <-> audio track only).
bool pool_clip_fits(vivid::App& app, int pool_i, int tt) {
    auto* s = app.session;
    return s && S::session_pool_is_audio(s, pool_i) == S::session_track_is_audio(s, tt);
}
// Place a clip-pool item into a grid cell (type must match: audio->audio, MIDI->instrument).
void place_pool_clip(vivid::App& app, int pool_i, int tt, int ts) {
    auto* s = app.session;
    if (!s || !pool_clip_fits(app, pool_i, tt)) return;
    if (S::session_pool_is_audio(s, pool_i)) { S::session_pool_place_audio(s, pool_i, tt, ts); return; }
    S::ClipNote buf[512];
    const int n = S::session_pool_get(s, pool_i, buf, 512);
    S::session_set_clip(s, tt, ts, buf, n, S::session_pool_length(s, pool_i));
}
// Stash a grid clip into the pool: MOVE it out of the session (the source cell is cleared).
// Handles both MIDI (instrument) and audio (sampler) tracks.
void stash_clip(vivid::App& app, int st, int ss) {
    auto* s = app.session;
    if (!s) return;
    char nm[28]; std::snprintf(nm, sizeof nm, "%.12s %c", S::session_track_name(s, st), 'A' + ss);
    if (S::session_track_is_audio(s, st)) { S::session_pool_stash_audio(s, st, ss, nm); return; }
    S::ClipNote buf[512];
    const int n = S::session_get_clip(s, st, ss, buf, 512);
    if (n <= 0) return;
    const double len = S::session_clip_length(s, st, ss);
    S::session_pool_add(s, buf, n, len, nm);
    S::session_set_clip(s, st, ss, nullptr, 0, len);   // take it out of the grid
}
}  // namespace

namespace vivid::input {

// On mouse release, complete a clip drag: pool->cell places, cell->cell moves (Alt = copy),
// cell->pool-bar stashes; a plain (non-drag) click on a grid clip launches it. Returns true when
// a clip/pool drag was in progress (it always clears the drag state).
bool clipgrid_release(Window& win, App& app, double mx, double my, int mods, int tracks, int scenes) {
    if (!(win.clip_drag_t >= 0 || win.clip_drag_from_pool >= 0)) return false;
    int tt = -1, ts = -1;
    const bool onCell = clip_cell_at(tracks, scenes, mx - win.sidebar_w, my, tt, ts);   // grid is shifted
    const bool onBar  = in_sidebar(win.sidebar_w, win.win_h, win.dock_h, mx, my);
    if (win.clip_dragging) {
        if (win.clip_drag_from_pool >= 0) {                     // pool -> grid cell
            if (onCell) place_pool_clip(app, win.clip_drag_from_pool, tt, ts);
        } else {                                                // grid cell -> ...
            const int st = win.clip_drag_t, ss = win.clip_drag_sc;
            if (onCell && (tt != st || ts != ss)) move_clip(app, st, ss, tt, ts, (mods & GLFW_MOD_ALT) != 0);
            else if (onBar) stash_clip(app, st, ss);            // grid cell -> pool (stash a copy)
        }
    } else if (win.clip_drag_t >= 0) {
        S::session_launch_clip(app.session, win.clip_drag_t, win.clip_drag_sc);  // plain click launches
    }
    win.clip_drag_t = -1; win.clip_drag_from_pool = -1; win.clip_dragging = false;
    return true;
}

// CLIPS panel press (inside the sidebar guard): remove (×) or arm a drag of a pool clip. Returns
// true when a pool item was hit (× removed or a drag armed).
bool clipgrid_pool_press(Window& win, App& app, double mx, double my) {
    const int nc = app.session ? S::session_pool_count(app.session) : 0;
    for (int i = 0; i < nc; ++i)
        if (hit(pool_item_x_rect(i, win.sidebar_w), mx, my)) { S::session_pool_remove(app.session, i); return true; }
    const int pi = pool_item_at(win.sidebar_w, nc, mx, my);
    if (pi >= 0 && pool_item_visible(pi, win.sidebar_w, win.win_h, win.dock_h)) {
        win.clip_drag_from_pool = pi; win.clip_dragging = false; win.clip_drag_x0 = mx; win.clip_drag_y0 = my;
        return true;
    }
    return false;
}

// Left-click a meter (master or per-track) -> open its characteristic menu. Returns true on hit.
bool clipgrid_meter_menu(Window& win, App& app, double mx, double my, int tracks, int scenes) {
    const int src = meter_hit(tracks, scenes, mx - win.sidebar_w, my);
    if (src != -2) { win.menu = { true, static_cast<float>(mx), static_cast<float>(my), src }; return true; }
    return false;
}

// Track header: × removes the track (drops its mappings + closes editor windows, since removal
// shifts indices); otherwise select it. "+ Track" opens the instrument picker. Returns true on hit.
bool clipgrid_track_header(Window& win, App& app, double mx, double my, int tracks) {
    const double dmx = mx - win.sidebar_w;
    for (int t = 0; t < tracks; ++t) {
        if (hit(track_header_x_rect(t), dmx, my)) {
            // Close all open instrument editor windows first: removal shifts track indices, so the
            // per-track window pool would otherwise misalign (they reopen on demand).
            for (int k = 0; k < S::kMaxTracks; ++k)
                if (win.track_win[k]) { vst3_plugin_window_close(win.track_win[k]); win.track_win[k] = nullptr; }
            const int rid = S::session_track_id(app.session, t);   // capture before removal
            if (S::session_remove_track(app.session, t)) {
                if (app.graph) app.graph->drop_track_sources(rid);   // drop this track's mappings (id-based)
                const int nt = S::session_track_count(app.session);
                if (win.sel_track >= nt) win.sel_track = std::max(0, nt - 1);
            }
            return true;
        }
        if (hit(track_header_rect(t), dmx, my)) { win.sel_track = t; if (app.graph) app.graph->select_op(-1); return true; }
    }
    if (tracks < S::kMaxTracks && hit(track_add_rect(tracks), dmx, my)) {
        win.track_menu = { true, static_cast<float>(mx), static_cast<float>(my), -1 };
        return true;
    }
    return false;
}

// Mixer strip: ARM buttons (record-arm toggle) then gain sliders (jump + start a drag). Returns
// true on hit.
bool clipgrid_mixer(Window& win, App& app, double mx, double my, int tracks, int scenes) {
    const double dmx = mx - win.sidebar_w;
    for (int t = 0; t < tracks; ++t) {
        if (hit(track_arm_rect(t, scenes), dmx, my)) {
            const bool armed = S::session_armed_track(app.session) == t;
            S::session_set_armed_track(app.session, armed ? -1 : t);
            return true;
        }
    }
    for (int t = 0; t < tracks; ++t) {
        const Rect gr = track_gain_rect(t, scenes);
        if (hit(gr, dmx, my)) {
            win.gain_drag = t;
            S::session_set_track_gain(app.session, t, std::min(1.0, std::max(0.0, (dmx - gr.x) / gr.w)));
            return true;
        }
    }
    return false;
}

// Clip grid: single click on a cell arms a drag (a plain click launches on release); double-click
// opens the docked editor. Then the scene-launch buttons launch a whole row. Returns true on hit.
bool clipgrid_cells(Window& win, App& app, double mx, double my, int tracks, int scenes) {
    const double dmx = mx - win.sidebar_w;
    for (int t = 0; t < tracks; ++t)
        for (int sc = 0; sc < scenes; ++sc)
            if (hit(clip_cell_rect(t, sc), dmx, my)) {
                const double now = glfwGetTime();
                if (win.editor && win.last_clip_track == t && win.last_clip_scene == sc && now - win.last_clip_t < 0.35) {
                    editor_open_clip(win, app, t, sc, tracks);   // double-click opens the docked editor
                    win.last_clip_t = -1; win.clip_drag_t = -1;
                    return true;
                }
                win.last_clip_t = now; win.last_clip_track = t; win.last_clip_scene = sc;
                // Arm a potential drag; a plain click launches on release, a drag moves the clip.
                win.clip_drag_t = t; win.clip_drag_sc = sc; win.clip_dragging = false;
                win.clip_drag_x0 = mx; win.clip_drag_y0 = my;
                return true;
            }
    for (int sc = 0; sc < scenes; ++sc)
        if (hit(scene_launch_rect(sc), dmx, my)) { S::session_launch_scene(app.session, sc); return true; }
    return false;
}

}  // namespace vivid::input
