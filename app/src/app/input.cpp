#include "app/input.h"
#include "app/input_internal.h"   // Phase D (#8): per-concern input controllers
#include "platform/platform.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window.h"
#include "ui/layout.h"
#include "ui/session_view.h"      // meter_hit
#include "ui/mapping_overview.h"  // ov_geom, ov_row
#include "ui/node_graph.h"
#include "ui/audio_node_graph.h"
#include "ui/clip_editor.h"
#include "audio/vst3_host.h"
#include "audio/plugin_catalog.h"
#include "app/frame.h"   // toggle_popout
#include "app/operator_clone.h"   // clone_operator / operator_has_clone_template
#include "transport.h"   // Transport play/stop (toggle_playing)
#include "audio/vst3_plugin_window.h"   // vst3_plugin_window_* + Steinberg::Vst::IEditController
#include "gpu/visual_graph.h"           // VOp, VisualGraph

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

using namespace vivid::ui;  // Rect/hit, layout helpers + constants, meter_hit, ov_*

// The File menu (New/Open/Save/Save As + Open Recent) is now a native OS menu — see
// platform/menu_bar.* + app/file_actions.*, wired in main.cpp. Its ⌘N/⌘O/⌘S/⇧⌘S key
// equivalents are handled by AppKit, so there's no File handling here anymore.

// Number keys 1..N launch scene 0..N-1 across all tracks (applied on the next bar).
void key_callback(GLFWwindow* w, int key, int /*sc*/, int action, int mods) {
    auto* win = static_cast<vivid::Window*>(glfwGetWindowUserPointer(w));
    if (!win) return;
    vivid::App* app = win->app;
    // The operator chooser captures the keyboard while open (repeat allowed for nav).
    if (app->graph && app->graph->chooser_open()) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            if (key == GLFW_KEY_ESCAPE) app->graph->chooser_hide();
            else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) app->graph->chooser_confirm();
            else if (key == GLFW_KEY_DOWN || key == GLFW_KEY_TAB) app->graph->chooser_move(+1);
            else if (key == GLFW_KEY_UP) app->graph->chooser_move(-1);
            else if (key == GLFW_KEY_BACKSPACE) app->graph->chooser_backspace();
        }
        return;  // swallow all keys while the chooser is up
    }
    // Musical typing (M6.2) — the ` toggle + note/octave/velocity keys. Runs before the PRESS-only
    // gate below because note-off needs the RELEASE event; unhandled keys fall through.
    if (vivid::input::typing_key(*win, *app, key, action)) return;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    // The clip editor, when open, gets first crack at keys (Esc close / Delete / undo / select-all
    // / tool). Unhandled keys fall through to the global shortcuts below.
    if (vivid::input::editor_key(*win, key, mods)) return;
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE && win->show_mappings) { win->show_mappings = false; return; }
    if (key == GLFW_KEY_M) { win->show_mappings = !win->show_mappings; return; }  // mapping overview
    if (vivid::input::transport_key(*win, *app, key)) return;   // Space (play/stop) / R (record)
    // Tab -> open the operator chooser at the cursor (visuals pane only).
    if (key == GLFW_KEY_TAB && app->graph) {
        double mx, my; glfwGetCursorPos(w, &mx, &my);
        if (win->show_graph && mx >= win->split_x) { app->graph->chooser_show(mx, my); return; }
    }

    // File shortcuts (⌘N/⌘O/⌘S/⇧⌘S) are owned by the native File menu (AppKit intercepts
    // them before GLFW), so they're not handled here.
    if (key == GLFW_KEY_V && app->vgraph) {  // toggle the visuals generator (also via the generator node)
        app->vgraph->set_generator(app->vgraph->generator() == vivid::VOp::Video ? vivid::VOp::Plasma : vivid::VOp::Video);
        return;
    }
    if (key == GLFW_KEY_N) { app->load_video_at(app->video_idx + 1); return; }  // next clip
    if (!app->session) return;
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
        int idx = key - GLFW_KEY_1;
        if (idx < vivid::session::session_scene_count(app->session)) {
            vivid::session::session_launch_scene(app->session, idx);
            std::fprintf(stderr, "[vivid] launch scene %c (queued for next bar)\n", 'A' + idx);
        }
    }
}

void char_callback(GLFWwindow* w, unsigned int cp) {
    auto* win = static_cast<vivid::Window*>(glfwGetWindowUserPointer(w));
    if (win && win->app->graph && win->app->graph->chooser_open()) win->app->graph->chooser_char(cp);
}

// GLFW gives no modifier state to scroll callbacks; poll the keys we care about.
static int scroll_mods(GLFWwindow* w) {
    int m = 0;
    if (glfwGetKey(w, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS) m |= GLFW_MOD_SUPER;
    if (glfwGetKey(w, GLFW_KEY_LEFT_ALT) == GLFW_PRESS   || glfwGetKey(w, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)   m |= GLFW_MOD_ALT;
    if (glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) m |= GLFW_MOD_SHIFT;
    return m;
}

void scroll_callback(GLFWwindow* w, double xoff, double yoff) {
    auto* win = static_cast<vivid::Window*>(glfwGetWindowUserPointer(w));
    if (!win) return;
    double mx, my; glfwGetCursorPos(w, &mx, &my);
    if (vivid::input::editor_scroll(*win, xoff, yoff, scroll_mods(w), mx, my)) return;
    // Scroll over the sidebar's PLUGINS panel scrolls the plugin list.
    if (vivid::input::plugins_scroll(*win, *win->app, yoff, mx, my)) return;
    // Zoom whichever node graph is under the cursor (visuals node graph / audio-graph deep view).
    vivid::input::graph_scroll(*win, *win->app, yoff, mx, my);
}

// The clip cell under (mx,my), or false. Fills t/sc on a hit.
bool clip_cell_at(int tracks, int scenes, double mx, double my, int& t, int& sc) {
    for (int a = 0; a < tracks; ++a)
        for (int b = 0; b < scenes; ++b)
            if (hit(clip_cell_rect(a, b), mx, my)) { t = a; sc = b; return true; }
    return false;
}
// Move (or copy) a MIDI clip from (st,ss) to (tt,ts) via the thread-safe clip API.
// Instrument tracks only (audio clips carry a sample and aren't relocatable here).
void move_clip(vivid::App& app, int st, int ss, int tt, int ts, bool copy) {
    auto* s = app.session;
    if (!s || vivid::session::session_track_is_audio(s, st) || vivid::session::session_track_is_audio(s, tt)) return;
    vivid::session::ClipNote buf[512];
    const int n = vivid::session::session_get_clip(s, st, ss, buf, 512);
    if (n <= 0) return;   // nothing to move
    const double len = vivid::session::session_clip_length(s, st, ss);
    vivid::session::session_set_clip(s, tt, ts, buf, n, len);
    if (!copy) vivid::session::session_set_clip(s, st, ss, nullptr, 0, len);   // clear the source
}
// Whether a pooled clip can be placed on a track (audio clip <-> audio track only).
bool pool_clip_fits(vivid::App& app, int pool_i, int tt) {
    auto* s = app.session;
    return s && vivid::session::session_pool_is_audio(s, pool_i) == vivid::session::session_track_is_audio(s, tt);
}
// Place a clip-pool item into a grid cell (type must match: audio->audio, MIDI->instrument).
void place_pool_clip(vivid::App& app, int pool_i, int tt, int ts) {
    auto* s = app.session;
    if (!s || !pool_clip_fits(app, pool_i, tt)) return;
    if (vivid::session::session_pool_is_audio(s, pool_i)) { vivid::session::session_pool_place_audio(s, pool_i, tt, ts); return; }
    vivid::session::ClipNote buf[512];
    const int n = vivid::session::session_pool_get(s, pool_i, buf, 512);
    vivid::session::session_set_clip(s, tt, ts, buf, n, vivid::session::session_pool_length(s, pool_i));
}
// Stash a grid clip into the pool: MOVE it out of the session (the source cell is cleared).
// Handles both MIDI (instrument) and audio (sampler) tracks.
void stash_clip(vivid::App& app, int st, int ss) {
    auto* s = app.session;
    if (!s) return;
    char nm[28]; std::snprintf(nm, sizeof nm, "%.12s %c", vivid::session::session_track_name(s, st), 'A' + ss);
    if (vivid::session::session_track_is_audio(s, st)) { vivid::session::session_pool_stash_audio(s, st, ss, nm); return; }
    vivid::session::ClipNote buf[512];
    const int n = vivid::session::session_get_clip(s, st, ss, buf, 512);
    if (n <= 0) return;
    const double len = vivid::session::session_clip_length(s, st, ss);
    vivid::session::session_pool_add(s, buf, n, len, nm);
    vivid::session::session_set_clip(s, st, ss, nullptr, 0, len);   // take it out of the grid
}

// Add a browsed plugin (auto-routed): try it as an instrument (a MIDI-in bus makes a
// new track); otherwise add it as an effect on the selected track. Loading happens here.
void mouse_button_callback(GLFWwindow* w, int button, int action, int mods) {
    auto* win = static_cast<vivid::Window*>(glfwGetWindowUserPointer(w));
    if (!win) return;
    vivid::App* app = win->app;
    double mx, my; glfwGetCursorPos(w, &mx, &my);
    const double dmx = mx - win->sidebar_w;   // DAW-pane coords (the grid is shifted right by the sidebar)
    const int tracks = app->session ? vivid::session::session_track_count(app->session) : 0;
    const int scenes = app->session ? vivid::session::session_scene_count(app->session) : 0;

    // Top transport bar: play/pause + record + metronome (M6).
    if (vivid::input::transport_mouse(*win, *app, button, action, mx, my)) return;
    // Browser sidebar toggle.
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && hit(vivid::ui::sidebar_toggle_rect(), mx, my)) {
        win->sidebar_w = (win->sidebar_w > 0.f) ? 0.f : vivid::ui::kSidebarW;
        return;
    }
    // Pop-out visuals window toggle (OUTPUT header).
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS
        && hit(vivid::ui::popout_button_rect(win->win_w, win->split_x), mx, my)) {
        vivid::toggle_popout(*app, *win);
        return;
    }
    // UI-2: reveal/hide the visuals node graph (a deep view under the output).
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS
        && hit(vivid::ui::graph_button_rect(win->win_w, win->split_x), mx, my)) {
        win->show_graph = !win->show_graph;
        if (!win->show_graph && app->graph) app->graph->select_op(-1);   // closing clears node focus
        return;
    }
    // (The File menu is now a native OS menu — see platform/menu_bar.*.)

    // Mapping overview is modal while open: per-row steppers/toggle/clear; click-away closes.
    if (win->show_mappings && app->graph && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        const auto& maps = app->graph->mappings();
        const OvGeom o = ov_geom(static_cast<int>(maps.size()), win->win_w);
        if (mx >= o.px && mx < o.px + o.w && my >= o.py && my < o.py + o.h) {
            for (int i = 0; i < o.vis; ++i) {
                const float ry = o.py + o.hdr + i * o.rowh;
                if (my < ry || my >= ry + o.rowh) continue;
                const OvRow rc = ov_row(o.px, o.w, ry);
                const std::string& d = maps[i].dest;
                if (hit(rc.inv, mx, my))      { app->graph->toggle_mapping_invert(d); return; }
                if (hit(rc.amtMinus, mx, my)) { app->graph->set_mapping_amount(d, std::max(0.f, maps[i].amount - 0.1f)); return; }
                if (hit(rc.amtPlus, mx, my))  { app->graph->set_mapping_amount(d, std::min(4.f, maps[i].amount + 0.1f)); return; }
                if (hit(rc.curMinus, mx, my)) { app->graph->set_mapping_curve(d, std::max(-1.f, maps[i].curve - 0.25f)); return; }
                if (hit(rc.curPlus, mx, my))  { app->graph->set_mapping_curve(d, std::min(1.f, maps[i].curve + 0.25f)); return; }
                if (hit(rc.loMinus, mx, my))  { app->graph->set_mapping_lo(d, std::max(0.f, maps[i].out_lo - 0.1f)); return; }
                if (hit(rc.loPlus, mx, my))   { app->graph->set_mapping_lo(d, std::min(1.f, maps[i].out_lo + 0.1f)); return; }
                if (hit(rc.hiMinus, mx, my))  { app->graph->set_mapping_hi(d, std::max(0.f, maps[i].out_hi - 0.1f)); return; }
                if (hit(rc.hiPlus, mx, my))   { app->graph->set_mapping_hi(d, std::min(1.f, maps[i].out_hi + 0.1f)); return; }
                if (hit(rc.clear, mx, my))    { app->graph->disconnect_dest(d); return; }
                break;
            }
            return;  // click inside the panel: consume
        }
        win->show_mappings = false; return;  // click outside: close
    }

    // Clip editor (non-modal): presses inside route to it; a release ends any editor drag.
    if (vivid::input::editor_mouse(*win, button, action, mx, my, mods)) return;

    // DAW | visuals splitter.
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) { win->split_drag = false; win->dock_drag = false; }
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && hit(win->dock_resize_rect(), mx, my)) {
        win->dock_drag = true; return;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && hit(win->splitter_rect(), mx, my)) {
        const double now = glfwGetTime();
        if (now - win->split_last_t < 0.35) { win->split_x = std::round(win->win_w * 0.46f); win->split_drag = false; win->split_last_t = -1.0; }
        else { win->split_drag = true; win->split_last_t = now; }
        return;
    }

    // Right-click a visuals op node -> its context menu (open source / clone).
    if (vivid::input::graph_node_rclick(*win, *app, button, action, mx, my)) return;
    // Right-click a meter (master or per-track) -> open its characteristic menu.
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        const int src = app->session ? meter_hit(tracks, scenes, mx - win->sidebar_w, my) : -2;
        if (src != -2) win->menu = { true, static_cast<float>(mx), static_cast<float>(my), src };
        else win->menu.open = false;
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    if (action == GLFW_RELEASE) {
        win->gain_drag = -1; win->param_drag = -1; win->ag_param_drag = -1; win->ag_panning = false;
        // Complete an audio-graph rewire: release over another node's input port connects the edge.
        if (vivid::input::graph_rewire_release(*win, *app, mx, my)) return;
        if (vivid::input::plugins_release(*win, *app, mx, my)) return;   // plugin drop (browser -> track / +Track)
        if (win->clip_drag_t >= 0 || win->clip_drag_from_pool >= 0) {   // clip drop (grid or pool source)
            int tt = -1, ts = -1;
            const bool onCell = clip_cell_at(tracks, scenes, mx - win->sidebar_w, my, tt, ts);   // grid is shifted
            const bool onBar  = vivid::ui::in_sidebar(win->sidebar_w, win->win_h, win->dock_h, mx, my);
            if (win->clip_dragging) {
                if (win->clip_drag_from_pool >= 0) {                     // pool -> grid cell
                    if (onCell) place_pool_clip(*app, win->clip_drag_from_pool, tt, ts);
                } else {                                                 // grid cell -> ...
                    const int st = win->clip_drag_t, ss = win->clip_drag_sc;
                    if (onCell && (tt != st || ts != ss)) move_clip(*app, st, ss, tt, ts, (mods & GLFW_MOD_ALT) != 0);
                    else if (onBar) stash_clip(*app, st, ss);            // grid cell -> pool (stash a copy)
                }
            } else if (win->clip_drag_t >= 0) {
                vivid::session::session_launch_clip(app->session, win->clip_drag_t, win->clip_drag_sc);  // plain click launches
            }
            win->clip_drag_t = -1; win->clip_drag_from_pool = -1; win->clip_dragging = false;
        }
        if (app->graph) app->graph->on_up(mx, my);
        return;
    }
    if (action != GLFW_PRESS) return;

    // Browser sidebar (left column): remove / drag a pool clip; consume clicks so they don't
    // fall through to the shifted DAW pane behind it.
    if (win->sidebar_w > 0.f && mx < win->sidebar_w && my >= vivid::ui::kTopBarH && my < win->dock_top()) {
        // CLIPS panel: remove (×) or arm a drag of a pool clip.
        const int nc = app->session ? vivid::session::session_pool_count(app->session) : 0;
        for (int i = 0; i < nc; ++i)
            if (hit(vivid::ui::pool_item_x_rect(i, win->sidebar_w), mx, my)) { vivid::session::session_pool_remove(app->session, i); return; }
        const int pi = vivid::ui::pool_item_at(win->sidebar_w, nc, mx, my);
        if (pi >= 0 && vivid::ui::pool_item_visible(pi, win->sidebar_w, win->win_h, win->dock_h)) {
            win->clip_drag_from_pool = pi; win->clip_dragging = false; win->clip_drag_x0 = mx; win->clip_drag_y0 = my; return;
        }
        // PLUGINS panel: double-click a row to add (auto-route), or drag a row onto a track/+Track.
        vivid::input::plugins_sidebar_press(*win, *app, mx, my);
        return;   // consume all clicks over the sidebar
    }

    // Menu has priority: pick a characteristic -> spawn a data node in the graph.
    if (win->menu.open) {
        for (int j = 0; j < kNumChars; ++j) {
            const Rect r = { win->menu.x, win->menu.y + j * 26.f, 184.f, 26.f };
            if (hit(r, mx, my) && app->graph) {
                const int src = win->menu.src;   // -1 master, else a session track index
                const char* sname = src < 0 ? "Master" : vivid::session::session_track_name(app->session, src);
                // Encode by the track's STABLE id (master stays -1) so the wire follows the track.
                const int sid = src < 0 ? -1 : vivid::session::session_track_id(app->session, src);
                std::string title = std::string(sname) + "  " + kChars[j].label;
                app->graph->add_data_node(title, char_id_for(sid, kChars[j].id));
                std::fprintf(stderr, "[vivid] bridge: spawned '%s %s' node\n", sname, kChars[j].label);
                break;
            }
        }
        win->menu.open = false;
        return;
    }
    // Node context menu: "Open source" (custom nodes) or "Clone & Edit" (built-ins).
    if (vivid::input::graph_nodemenu(*win, *app, mx, my)) return;
    // FX menu has priority: pick an effect -> add it to the menu's track. Rows are the
    // VST3 catalog first, then native audio operators (matches draw_fx_menu ordering).
    if (win->fx_menu.open) {
        // Graph mode: native effects only, added via the graph edit API (authoritative). Device
        // mode: the VST3 catalog first, then native operators (matches draw_fx_menu ordering).
        const int nvst = win->fx_menu.graph ? 0 : vivid::session::session_available_effect_count();
        const int nnat = vivid::session::session_available_audio_op_count(app->session, 0);
        for (int j = 0; j < nvst + nnat; ++j) {
            const Rect r = { win->fx_menu.x, win->fx_menu.y + j * 24.f, 150.f, 24.f };
            if (hit(r, mx, my)) {
                if (j < nvst) vivid::session::session_add_effect_by_index(app->session, win->fx_menu.src, j);
                else {
                    const char* op = vivid::session::session_available_audio_op_name(app->session, 0, j - nvst);
                    if (win->fx_menu.graph) vivid::session::session_audio_graph_add_op(app->session, win->fx_menu.src, op);
                    else                    vivid::session::session_add_audio_effect(app->session, win->fx_menu.src, op);
                }
                break;
            }
        }
        win->fx_menu.open = false;
        return;
    }
    // Track menu: pick an instrument (or "Audio track") -> create the track.
    if (win->track_menu.open) {
        const int n = vivid::session::session_available_instrument_count();
        for (int j = 0; j <= n; ++j) {
            const Rect r = { win->track_menu.x, win->track_menu.y + j * 24.f, 150.f, 24.f };
            if (hit(r, mx, my)) {
                if (j == n) vivid::session::session_add_audio_track(app->session);
                else        vivid::session::session_add_instrument_track(app->session, vivid::session::session_available_instrument_name(j));
                break;
            }
        }
        win->track_menu.open = false;
        return;
    }
    // Map menu: pick a source to drive the selected param (the return path).
    if (win->map_menu.open) {
        const int seltr = std::min(std::max(win->sel_track, 0), tracks - 1);
        const DevSlot seldev = dock_resolve(app->session, seltr, std::max(0, win->sel_device));
        for (int j = 0; j < kNumMapSources; ++j) {
            const Rect rr = { win->map_menu.x, win->map_menu.y + j * 24.f, 168.f, 24.f };
            if (hit(rr, mx, my) && app->graph) {
                const std::string d = dock_param_dest(seltr, seldev, win->map_param);
                if (kMapSources[j].id[0] == '\0') app->graph->disconnect_dest(d);
                else app->graph->add_mapping(kMapSources[j].id, d, 1.0f);
                break;
            }
        }
        win->map_menu.open = false;
        return;
    }
    if (!app->session) return;

    // A meter (master or per-track) -> open its characteristic menu (left-click).
    {
        const int src = meter_hit(tracks, scenes, dmx, my);
        if (src != -2) { win->menu = { true, static_cast<float>(mx), static_cast<float>(my), src }; return; }
    }
    // Track header: × removes the track; otherwise select it. "+ Track" opens the picker.
    for (int t = 0; t < tracks; ++t) {
        if (hit(track_header_x_rect(t), dmx, my)) {
            // Close all open instrument editor windows first: removal shifts track indices,
            // so the per-track window pool would otherwise misalign (they reopen on demand).
            for (int k = 0; k < vivid::session::kMaxTracks; ++k)
                if (win->track_win[k]) { vst3_plugin_window_close(win->track_win[k]); win->track_win[k] = nullptr; }
            const int rid = vivid::session::session_track_id(app->session, t);   // capture before removal
            if (vivid::session::session_remove_track(app->session, t)) {
                if (app->graph) app->graph->drop_track_sources(rid);   // drop this track's mappings (id-based)
                const int nt = vivid::session::session_track_count(app->session);
                if (win->sel_track >= nt) win->sel_track = std::max(0, nt - 1);
            }
            return;
        }
        if (hit(track_header_rect(t), dmx, my)) { win->sel_track = t; if (app->graph) app->graph->select_op(-1); return; }
    }
    if (tracks < vivid::session::kMaxTracks && hit(track_add_rect(tracks), dmx, my)) {
        win->track_menu = { true, static_cast<float>(mx), static_cast<float>(my), -1 };
        return;
    }

    // Bottom dock interactions. If a visual node is selected, the dock is its
    // inspector: knobs edit the node's base param values (vertical drag). Routed through the
    // explicit focus (UI-1) — the single source of truth shared with the draw path — not a
    // re-derived selected_op. A close (x) in the header exits the focus back to the device view.
    if (win->focus.kind == vivid::FocusContext::Kind::VisualNode && app->graph && my >= win->dock_top()) {
        if (hit(dock_close_rect(win->win_w, win->win_h, win->dock_h), mx, my)) {
            app->graph->select_op(-1); return;   // close the visual-node inspector -> device view
        }
    }
    // UI-3 audio node graph deep view: all its dock interaction (select / param / +FX / remove /
    // rewire / edge-disconnect / pan) + the Device header "Graph" drill-in button.
    if (vivid::input::graph_audio_dock(*win, *app, button, action, mx, my)) return;
    if (vivid::input::graph_drill_in(*win, *app, button, action, mx, my)) return;
    {
        if (win->focus.kind == vivid::FocusContext::Kind::VisualNode && app->graph && my >= win->dock_top()) {   // consume clicks inside the dock
            auto* g = app->graph;
            const int selop = win->focus.node;
            const int pc = g->op_param_count_at(selop);
            for (int i = 0; i < pc; ++i) {
                const int hint = g->op_param_hint_at(selop, i);
                if (hint == VIVID_DISPLAY_HIDDEN || hint == VIVID_DISPLAY_EDITOR || hint == VIVID_DISPLAY_TRANSIENT) continue;
                const Rect wr = node_param_widget_rect(i, win->win_w, win->win_h, win->dock_h);
                if (!hit(wr, mx, my)) continue;
                const float base = g->op_param_base_at(selop, i);
                switch (node_widget_kind(g->op_param_type_at(selop, i), hint, g->op_param_choice_count_at(selop, i))) {
                    case NodeWidget::Toggle:
                        g->set_op_param_base_at(selop, i, base >= 0.5f ? 0.f : 1.f);
                        break;
                    case NodeWidget::Enum: {
                        const int cc = g->op_param_choice_count_at(selop, i);
                        if (cc > 1) { int idx = (int(std::lround(base * (cc - 1))) + 1) % cc; g->set_op_param_base_at(selop, i, float(idx) / (cc - 1)); }
                        break;
                    }
                    case NodeWidget::Slider:
                        g->set_op_param_base_at(selop, i, std::clamp((mx - wr.x) / wr.w, 0.0, 1.0));   // jump to click
                        win->param_drag = i; win->param_is_node = true; win->param_drag_horiz = true;
                        win->param_drag_v0 = 0.f; win->param_drag_y0 = my; break;
                    default:  // Knob: vertical drag
                        win->param_drag = i; win->param_is_node = true; win->param_drag_horiz = false;
                        win->param_drag_v0 = base; win->param_drag_y0 = my; break;
                }
                return;
            }
            return;  // node inspector showing — consume dock clicks
        }
    }
    // Otherwise the dock is the selected track's device chain: single-click selects
    // (shows params), double-click opens the plugin editor; x removes; + FX adds.
    {
        const int seltr = std::min(std::max(win->sel_track, 0), tracks - 1);
        const double now = glfwGetTime();
        auto open_dev = [&](int dev) {
            auto* ctrl = static_cast<Steinberg::Vst::IEditController*>(
                dev == 0 ? vivid::session::session_track_controller(app->session, seltr)
                         : vivid::session::session_effect_controller(app->session, seltr, dev - 1));
            if (!ctrl) return;
            const char* nm = dev == 0 ? vivid::session::session_track_name(app->session, seltr)
                                      : vivid::session::session_effect_name(app->session, seltr, dev - 1);
            if (dev == 0) {
                if (win->track_win[seltr]) { vst3_plugin_window_close(win->track_win[seltr]); win->track_win[seltr] = nullptr; }
                win->track_win[seltr] = vst3_plugin_window_open(ctrl, nm);
            } else {
                int slot = -1; for (int k = 0; k < 8; ++k) if (!win->fx_win[k]) { slot = k; break; }
                if (slot >= 0) win->fx_win[slot] = vst3_plugin_window_open(ctrl, nm);
            }
        };
        auto click_dev = [&](int dev) {
            if (win->last_dev_i == dev && now - win->last_dev_t < 0.35) { open_dev(dev); win->last_dev_t = -1; }
            else { win->sel_device = dev; win->last_dev_i = dev; win->last_dev_t = now; }
        };
        // device chips in the bottom dock (unified: instrument, VST3 fx, native fx, + FX)
        const int ndev = dock_device_count(app->session, seltr);
        for (int i = 0; i < ndev; ++i) {
            const DevSlot slot = dock_resolve(app->session, seltr, i);
            if (!slot.valid) continue;
            if (slot.is_instrument) {
                if (vivid::session::session_track_is_audio(app->session, seltr) && !slot.native) continue;  // no VST3 instrument on audio tracks
                if (hit(win->dock_chip(i), mx, my)) { click_dev(i); return; }
                continue;
            }
            if (hit(win->dock_chip_x(i), mx, my)) {   // remove × (VST3 or native effect)
                if (slot.native) vivid::session::session_remove_audio_effect(app->session, seltr, slot.api_index);
                else             vivid::session::session_remove_effect(app->session, seltr, slot.api_index - 1);
                if (win->sel_device >= ndev - 1) win->sel_device = 0;
                return;
            }
            if (hit(win->dock_chip(i), mx, my)) { click_dev(i); return; }
        }
        if (hit(win->dock_chip(ndev), mx, my)) {   // "+ FX" tile
            win->fx_menu = { true, static_cast<float>(mx), static_cast<float>(my), seltr };
            return;
        }
        // param knobs of the selected device (vertical drag; small map affordance)
        const DevSlot seldev = dock_resolve(app->session, seltr, std::max(0, win->sel_device));
        const DockGeom d = win->dock_geom();
        const int npc = std::min(dock_param_count(app->session, seltr, seldev), d.cols * d.maxRows);
        for (int i = 0; i < npc; ++i) {
            if (hit(dock_knob_map(i, d), mx, my)) {
                win->map_menu = { true, static_cast<float>(mx), static_cast<float>(my), 0 };
                win->map_param = i; return;
            }
            float cx, cy; dock_knob(i, d, cx, cy);
            if (std::hypot(mx - cx, my - cy) <= 16.0) {
                win->param_drag = i; win->param_is_node = false;
                win->param_drag_v0 = dock_param_norm(app->session, seltr, seldev, i);
                win->param_drag_y0 = my;
                return;
            }
        }
    }
    // mixer: ARM buttons (record-arm; toggling re-arms/disarms). Audio tracks are ignored
    // by the engine (no instrument), so arming one is a harmless no-op.
    for (int t = 0; t < tracks; ++t) {
        if (hit(track_arm_rect(t, scenes), dmx, my)) {
            const bool armed = vivid::session::session_armed_track(app->session) == t;
            vivid::session::session_set_armed_track(app->session, armed ? -1 : t);
            return;
        }
    }
    // mixer gain sliders
    for (int t = 0; t < tracks; ++t) {
        const Rect gr = track_gain_rect(t, scenes);
        if (hit(gr, dmx, my)) {
            win->gain_drag = t;
            vivid::session::session_set_track_gain(app->session, t, std::min(1.0, std::max(0.0, (dmx - gr.x) / gr.w)));
            return;
        }
    }
    if (win->show_graph && app->graph && app->graph->on_down(mx, my)) return;  // node graph consumed it
    // clip cells -> single click launches; double click opens the MIDI editor
    for (int t = 0; t < tracks; ++t)
        for (int sc = 0; sc < scenes; ++sc)
            if (hit(clip_cell_rect(t, sc), dmx, my)) {
                const double now = glfwGetTime();
                if (win->editor && win->last_clip_track == t && win->last_clip_scene == sc && now - win->last_clip_t < 0.35) {
                    vivid::input::editor_open_clip(*win, *app, t, sc, tracks);   // double-click opens the docked editor
                    win->last_clip_t = -1; win->clip_drag_t = -1;
                    return;
                }
                win->last_clip_t = now; win->last_clip_track = t; win->last_clip_scene = sc;
                // Arm a potential drag; a plain click launches on release, a drag moves the clip.
                win->clip_drag_t = t; win->clip_drag_sc = sc; win->clip_dragging = false;
                win->clip_drag_x0 = mx; win->clip_drag_y0 = my;
                return;
            }
    // scene launch buttons -> launch the whole row
    for (int sc = 0; sc < scenes; ++sc)
        if (hit(scene_launch_rect(sc), dmx, my)) { vivid::session::session_launch_scene(app->session, sc); return; }
}

}  // namespace

namespace vivid {
void install_input_callbacks(GLFWwindow* w) {
    glfwSetKeyCallback(w, key_callback);
    glfwSetCharCallback(w, char_callback);
    glfwSetMouseButtonCallback(w, mouse_button_callback);
    glfwSetScrollCallback(w, scroll_callback);
}
}  // namespace vivid
