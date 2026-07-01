#include "app/input.h"
#include "platform/platform.h"
#include "platform/file_dialog.h"   // native Open/Save panels (File menu)
#include <filesystem>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/project_io.h"   // folder-aware save/load + project-local operators
#include "app/window.h"
#include "ui/layout.h"
#include "ui/session_view.h"      // meter_hit
#include "ui/mapping_overview.h"  // ov_geom, ov_row
#include "ui/node_graph.h"
#include "ui/clip_editor.h"
#include "audio/vst3_host.h"
#include "transport.h"   // Transport play/stop (toggle_playing)
#include "audio/vst3_plugin_window.h"   // vst3_plugin_window_* + Steinberg::Vst::IEditController
#include "gpu/visual_graph.h"           // VOp, VisualGraph
#include "persist.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

using namespace vivid::ui;  // Rect/hit, layout helpers + constants, meter_hit, ov_*

std::string default_project_path() {
    return (std::filesystem::path(vivid::platform::user_data_dir()) / "vivid_session.json").string();
}

void handle_default_project_shortcut(GLFWwindow* w, vivid::Window& win, vivid::App& app, bool save) {
    if (!app.session || !app.graph) return;
    const std::string path = default_project_path();
    if (save) {
        const bool ok = vivid::save_session(path, app.session, *app.graph, win.win_w, win.win_h, win.split_x, win.dock_h);
        if (ok) app.remember_project_path(path);
        std::fprintf(stderr, "[vivid] save %s: %s\n", path.c_str(), ok ? "ok" : "FAILED");
        return;
    }

    int ww = win.win_w, wh = win.win_h;
    float sxx = win.split_x, dh = win.dock_h;
    const bool ok = vivid::load_session(path, app.session, *app.graph, ww, wh, sxx, dh);
    if (ok) {
        app.remember_project_path(path);
        win.split_x = sxx;
        win.dock_h = dh;
        glfwSetWindowSize(w, ww, wh);
    }
    std::fprintf(stderr, "[vivid] load %s: %s\n", path.c_str(), ok ? "ok" : "FAILED");
}

// File-menu actions (arbitrary paths, unlike the fixed-path Cmd+S/O). new = fresh slate.
void file_new(vivid::App& app) {
    if (!app.session || !app.graph) return;
    const int nt = vivid::session::session_track_count(app.session);
    const int ns = vivid::session::session_scene_count(app.session);
    for (int t = 0; t < nt; ++t)
        for (int sc = 0; sc < ns; ++sc)
            vivid::session::session_set_clip(app.session, t, sc, nullptr, 0, 4.0);
    app.graph->reset_nodes();
    if (app.vgraph) { app.vgraph->reset_to_default(); app.vgraph->set_asset_dir(""); }
    app.project.current_project_path.clear();
    app.project.media_root.clear();
    app.project.missing_media.clear();
}
void file_open(GLFWwindow* w, vivid::Window& win, vivid::App& app, const std::string& path) {
    if (path.empty() || !app.session || !app.graph) return;
    int ww = win.win_w, wh = win.win_h; float sxx = win.split_x, dh = win.dock_h;
    auto lr = vivid::project_io::load(app, *app.graph, ww, wh, sxx, dh, path);  // folder-aware (+ project-local ops)
    if (lr.ok) {
        win.split_x = sxx; win.dock_h = dh; glfwSetWindowSize(w, ww, wh);
    }
    std::fprintf(stderr, "[vivid] open %s: %s\n", path.c_str(), lr.ok ? "ok" : lr.error.c_str());
}
void file_save(vivid::Window& win, vivid::App& app, const std::string& path) {
    if (path.empty() || !app.session || !app.graph) return;
    auto sr = vivid::project_io::save(app, *app.graph, win.win_w, win.win_h, win.split_x, win.dock_h, path);
    std::fprintf(stderr, "[vivid] save %s: %s\n", path.c_str(), sr.ok ? "ok" : sr.error.c_str());
}

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
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE && win->editor && win->editor->is_open()) { win->editor->close(); return; }
    if (key == GLFW_KEY_ESCAPE && win->show_mappings) { win->show_mappings = false; return; }
    if (key == GLFW_KEY_M) { win->show_mappings = !win->show_mappings; return; }  // mapping overview
    if (key == GLFW_KEY_SPACE && app->transport) { app->transport->toggle_playing(); return; }  // play/stop
    // Tab -> open the operator chooser at the cursor (visuals pane only).
    if (key == GLFW_KEY_TAB && app->graph) {
        double mx, my; glfwGetCursorPos(w, &mx, &my);
        if (mx >= win->split_x) { app->graph->chooser_show(mx, my); return; }
    }

    // Cmd+S / Cmd+O -> save / load the session (in the per-user data dir).
    if ((mods & GLFW_MOD_SUPER) && key == GLFW_KEY_N && app->session && app->graph) { file_new(*app); return; }  // New
    if ((mods & GLFW_MOD_SUPER) && app->session && app->graph && (key == GLFW_KEY_S || key == GLFW_KEY_O)) {
        handle_default_project_shortcut(w, *win, *app, key == GLFW_KEY_S);
        return;
    }
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

void scroll_callback(GLFWwindow* w, double /*xoff*/, double yoff) {
    auto* win = static_cast<vivid::Window*>(glfwGetWindowUserPointer(w));
    if (!win) return;
    double mx, my; glfwGetCursorPos(w, &mx, &my);
    if (win->editor && win->editor->is_open() && win->editor->contains(mx, my)) { win->editor->scroll(yoff); return; }
    // Scroll over the visuals pane zooms the node graph around the cursor.
    if (win->app->graph && mx >= win->split_x) win->app->graph->zoom_at(mx, my, std::pow(1.12f, static_cast<float>(yoff)));
}

void mouse_button_callback(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* win = static_cast<vivid::Window*>(glfwGetWindowUserPointer(w));
    if (!win) return;
    vivid::App* app = win->app;
    double mx, my; glfwGetCursorPos(w, &mx, &my);
    const int tracks = app->session ? vivid::session::session_track_count(app->session) : 0;
    const int scenes = app->session ? vivid::session::session_scene_count(app->session) : 0;

    // Top transport bar: the play/pause button.
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && app->transport
        && hit(vivid::ui::transport_play_rect(), mx, my)) {
        app->transport->toggle_playing();
        return;
    }
    // File menu: dispatch an item (or click-away closes) when open.
    if (win->file_menu.open) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            const int nrec = std::min(static_cast<int>(app->project.recent_project_paths.size()), 8);
            for (int j = 0; j < vivid::ui::kFileMenuFixed + nrec; ++j) {
                const Rect r = { win->file_menu.x, win->file_menu.y + j * 24.f, 170.f, 24.f };
                if (!hit(r, mx, my)) continue;
                if (j == 0) file_new(*app);                                             // New
                else if (j == 1) file_open(w, *win, *app, vivid::platform::open_project_dialog());  // Open…
                else if (j == 2) file_save(*win, *app, app->project.current_project_path.empty()
                                           ? vivid::platform::save_project_dialog("project.vivid.json")
                                           : app->project.current_project_path);         // Save
                else if (j == 3) file_save(*win, *app, vivid::platform::save_project_dialog("project.vivid.json"));  // Save As…
                else file_open(w, *win, *app, app->project.recent_project_paths[j - vivid::ui::kFileMenuFixed]);     // Recent
                break;
            }
            win->file_menu.open = false;
            return;
        }
    }
    // Open the File menu on the File button.
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && hit(vivid::ui::transport_file_rect(), mx, my)) {
        const Rect fr = vivid::ui::transport_file_rect();
        win->file_menu = { true, fr.x, fr.y + fr.h + 2.f, -1 };
        return;
    }

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

    // Clip editor is non-modal: route presses inside its panel to it; clicks
    // elsewhere pass through to the session. A release always ends any editor drag.
    if (win->editor && win->editor->is_open()) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) win->editor->on_up(mx, my);
        if (action == GLFW_PRESS && win->editor->contains(mx, my)) {
            if (button == GLFW_MOUSE_BUTTON_LEFT) win->editor->on_down(mx, my, glfwGetTime());
            return;
        }
    }

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

    // Right-click a meter (master or per-track) -> open its characteristic menu.
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        const int src = app->session ? meter_hit(tracks, scenes, mx, my) : -2;
        if (src != -2) win->menu = { true, static_cast<float>(mx), static_cast<float>(my), src };
        else win->menu.open = false;
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    if (action == GLFW_RELEASE) { win->gain_drag = -1; win->param_drag = -1; if (app->graph) app->graph->on_up(mx, my); return; }
    if (action != GLFW_PRESS) return;

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
    // FX menu has priority: pick an effect -> add it to the menu's track.
    if (win->fx_menu.open) {
        for (int j = 0; j < vivid::session::session_available_effect_count(); ++j) {
            const Rect r = { win->fx_menu.x, win->fx_menu.y + j * 24.f, 150.f, 24.f };
            if (hit(r, mx, my)) { vivid::session::session_add_effect_by_index(app->session, win->fx_menu.src, j); break; }
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
        const int seldev = std::max(0, win->sel_device);
        for (int j = 0; j < kNumMapSources; ++j) {
            const Rect rr = { win->map_menu.x, win->map_menu.y + j * 24.f, 168.f, 24.f };
            if (hit(rr, mx, my) && app->graph) {
                const std::string d = param_dest(seltr, seldev, win->map_param);
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
        const int src = meter_hit(tracks, scenes, mx, my);
        if (src != -2) { win->menu = { true, static_cast<float>(mx), static_cast<float>(my), src }; return; }
    }
    // Track header: × removes the track; otherwise select it. "+ Track" opens the picker.
    for (int t = 0; t < tracks; ++t) {
        if (hit(track_header_x_rect(t), mx, my)) {
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
        if (hit(track_header_rect(t), mx, my)) { win->sel_track = t; if (app->graph) app->graph->select_op(-1); return; }
    }
    if (tracks < vivid::session::kMaxTracks && hit(track_add_rect(tracks), mx, my)) {
        win->track_menu = { true, static_cast<float>(mx), static_cast<float>(my), -1 };
        return;
    }

    // Bottom dock interactions. If a visual node is selected, the dock is its
    // inspector: knobs edit the node's base param values (vertical drag).
    {
        const int selop = app->graph ? app->graph->selected_op() : -1;
        if (selop >= 0 && my >= win->dock_top()) {   // only consume clicks inside the dock
            const DockGeom d = win->dock_geom();
            const int pc = app->graph->op_param_count_at(selop);
            for (int i = 0; i < pc; ++i) {
                float cx, cy; dock_knob(i, d, cx, cy);
                if (std::hypot(mx - cx, my - cy) <= 16.0) {
                    win->param_drag = i; win->param_is_node = true;
                    win->param_drag_v0 = app->graph->op_param_base_at(selop, i);
                    win->param_drag_y0 = my; return;
                }
            }
            return;  // node inspector showing — consume dock clicks
        }
    }
    // Otherwise the dock is the selected track's device chain: single-click selects
    // (shows params), double-click opens the plugin editor; x removes; + FX adds.
    {
        const int seltr = std::min(std::max(win->sel_track, 0), tracks - 1);
        const int nfx = vivid::session::session_effect_count(app->session, seltr);
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
        // device chips in the bottom dock
        if (!vivid::session::session_track_is_audio(app->session, seltr) && hit(win->dock_chip(0), mx, my)) { click_dev(0); return; }
        for (int e = 0; e < nfx; ++e) {
            if (hit(win->dock_chip_x(1 + e), mx, my)) {
                vivid::session::session_remove_effect(app->session, seltr, e);
                if (win->sel_device > nfx - 1) win->sel_device = 0;
                return;
            }
            if (hit(win->dock_chip(1 + e), mx, my)) { click_dev(1 + e); return; }
        }
        if (hit(win->dock_chip(1 + nfx), mx, my)) {
            win->fx_menu = { true, static_cast<float>(mx), static_cast<float>(my), seltr };
            return;
        }
        // param knobs of the selected device (vertical drag; small map affordance)
        const int seldev = std::max(0, win->sel_device);
        const DockGeom d = win->dock_geom();
        const int npc = std::min(vivid::session::session_param_count(app->session, seltr, seldev), d.cols * d.maxRows);
        for (int i = 0; i < npc; ++i) {
            if (hit(dock_knob_map(i, d), mx, my)) {
                win->map_menu = { true, static_cast<float>(mx), static_cast<float>(my), 0 };
                win->map_param = i; return;
            }
            float cx, cy; dock_knob(i, d, cx, cy);
            if (std::hypot(mx - cx, my - cy) <= 16.0) {
                win->param_drag = i; win->param_is_node = false;
                win->param_drag_v0 = vivid::session::session_param_value(app->session, seltr, seldev, i);
                win->param_drag_y0 = my;
                return;
            }
        }
    }
    // mixer gain sliders
    for (int t = 0; t < tracks; ++t) {
        const Rect gr = track_gain_rect(t, scenes);
        if (hit(gr, mx, my)) {
            win->gain_drag = t;
            vivid::session::session_set_track_gain(app->session, t, std::min(1.0, std::max(0.0, (mx - gr.x) / gr.w)));
            return;
        }
    }
    if (app->graph && app->graph->on_down(mx, my)) return;  // node graph consumed it
    // clip cells -> single click launches; double click opens the MIDI editor
    for (int t = 0; t < tracks; ++t)
        for (int sc = 0; sc < scenes; ++sc)
            if (hit(clip_cell_rect(t, sc), mx, my)) {
                const double now = glfwGetTime();
                if (win->editor && win->last_clip_track == t && win->last_clip_scene == sc && now - win->last_clip_t < 0.35) {
                    char title[80];
                    std::snprintf(title, sizeof title, "%s  \xC2\xB7  Clip %c",
                                  vivid::session::session_track_name(app->session, t), 'A' + sc);
                    if (vivid::session::session_track_is_audio(app->session, t)) {  // waveform editor
                        float bins[512]; float a = 0.f, b = 1.f;
                        const int nb = vivid::session::session_audio_waveform(app->session, t, sc, bins, 512);
                        vivid::session::session_get_audio_trim(app->session, t, sc, &a, &b);
                        win->editor->open_audio(t, sc, title, bins, nb, a, b);
                    } else {                                                  // piano-roll editor
                        vivid::session::ClipNote buf[256];
                        const int n = vivid::session::session_get_clip(app->session, t, sc, buf, 256);
                        const double len = vivid::session::session_clip_length(app->session, t, sc);
                        win->editor->open(t, sc, title, buf, n, len);
                    }
                    win->last_clip_t = -1;
                    return;
                }
                win->last_clip_t = now; win->last_clip_track = t; win->last_clip_scene = sc;
                vivid::session::session_launch_clip(app->session, t, sc);
                return;
            }
    // scene launch buttons -> launch the whole row
    for (int sc = 0; sc < scenes; ++sc)
        if (hit(scene_launch_rect(sc), mx, my)) { vivid::session::session_launch_scene(app->session, sc); return; }
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
