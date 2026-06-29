#include "app/input.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app_state.h"
#include "app/shell.h"
#include "ui/layout.h"
#include "ui/session_view.h"      // meter_hit
#include "ui/mapping_overview.h"  // ov_geom, ov_row
#include "ui/node_graph.h"
#include "ui/clip_editor.h"
#include "audio/vst3_host.h"
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

// Number keys 1..N launch scene 0..N-1 across all tracks (applied on the next bar).
void key_callback(GLFWwindow* w, int key, int /*sc*/, int action, int mods) {
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (!st) return;
    // The operator chooser captures the keyboard while open (repeat allowed for nav).
    if (st->graph && st->graph->chooser_open()) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            if (key == GLFW_KEY_ESCAPE) st->graph->chooser_hide();
            else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) st->graph->chooser_confirm();
            else if (key == GLFW_KEY_DOWN || key == GLFW_KEY_TAB) st->graph->chooser_move(+1);
            else if (key == GLFW_KEY_UP) st->graph->chooser_move(-1);
            else if (key == GLFW_KEY_BACKSPACE) st->graph->chooser_backspace();
        }
        return;  // swallow all keys while the chooser is up
    }
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE && st->editor && st->editor->is_open()) { st->editor->close(); return; }
    if (key == GLFW_KEY_ESCAPE && g_show_mappings) { g_show_mappings = false; return; }
    if (key == GLFW_KEY_M) { g_show_mappings = !g_show_mappings; return; }  // mapping overview
    // Tab -> open the operator chooser at the cursor (visuals pane only).
    if (key == GLFW_KEY_TAB && st->graph) {
        double mx, my; glfwGetCursorPos(w, &mx, &my);
        if (mx >= g_split_x) { st->graph->chooser_show(mx, my); return; }
    }

    // Cmd+S / Cmd+O -> save / load the session (~/vivid_session.json).
    if ((mods & GLFW_MOD_SUPER) && st->session && st->graph && (key == GLFW_KEY_S || key == GLFW_KEY_O)) {
        const char* home = std::getenv("HOME");
        const std::string path = std::string(home ? home : ".") + "/vivid_session.json";
        if (key == GLFW_KEY_S) {
            const bool ok = vivid::save_session(path, st->session, *st->graph, g_win_w, g_win_h, g_split_x, g_dock_h);
            std::fprintf(stderr, "[vivid] save %s: %s\n", path.c_str(), ok ? "ok" : "FAILED");
        } else {
            int ww = g_win_w, wh = g_win_h; float sxx = g_split_x, dh = g_dock_h;
            const bool ok = vivid::load_session(path, st->session, *st->graph, ww, wh, sxx, dh);
            if (ok) { g_split_x = sxx; g_dock_h = dh; glfwSetWindowSize(w, ww, wh); }
            std::fprintf(stderr, "[vivid] load %s: %s\n", path.c_str(), ok ? "ok" : "FAILED");
        }
        return;
    }
    if (key == GLFW_KEY_V && g_vgraph) {  // toggle the visuals generator (also via the generator node)
        g_vgraph->set_generator(g_vgraph->generator() == vivid::VOp::Video ? vivid::VOp::Plasma : vivid::VOp::Video);
        return;
    }
    if (key == GLFW_KEY_N) { load_video_at(g_video_idx + 1); return; }  // next clip
    if (!st->session) return;
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
        int idx = key - GLFW_KEY_1;
        if (idx < vivid_poc::session_scene_count(st->session)) {
            vivid_poc::session_launch_scene(st->session, idx);
            std::fprintf(stderr, "[vivid] launch scene %c (queued for next bar)\n", 'A' + idx);
        }
    }
}

void char_callback(GLFWwindow* w, unsigned int cp) {
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (st && st->graph && st->graph->chooser_open()) st->graph->chooser_char(cp);
}

void scroll_callback(GLFWwindow* w, double /*xoff*/, double yoff) {
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (!st) return;
    double mx, my; glfwGetCursorPos(w, &mx, &my);
    if (st->editor && st->editor->is_open() && st->editor->contains(mx, my)) { st->editor->scroll(yoff); return; }
    // Scroll over the visuals pane zooms the node graph around the cursor.
    if (st->graph && mx >= g_split_x) st->graph->zoom_at(mx, my, std::pow(1.12f, static_cast<float>(yoff)));
}

void mouse_button_callback(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* st = static_cast<AudioState*>(glfwGetWindowUserPointer(w));
    if (!st) return;
    double mx, my; glfwGetCursorPos(w, &mx, &my);
    const int tracks = st->session ? vivid_poc::session_track_count(st->session) : 0;
    const int scenes = st->session ? vivid_poc::session_scene_count(st->session) : 0;

    // Mapping overview is modal while open: per-row steppers/toggle/clear; click-away closes.
    if (g_show_mappings && st->graph && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        const auto& maps = st->graph->mappings();
        const OvGeom o = ov_geom(static_cast<int>(maps.size()), g_win_w);
        if (mx >= o.px && mx < o.px + o.w && my >= o.py && my < o.py + o.h) {
            for (int i = 0; i < o.vis; ++i) {
                const float ry = o.py + o.hdr + i * o.rowh;
                if (my < ry || my >= ry + o.rowh) continue;
                const OvRow rc = ov_row(o.px, o.w, ry);
                const std::string& d = maps[i].dest;
                if (hit(rc.inv, mx, my))      { st->graph->toggle_mapping_invert(d); return; }
                if (hit(rc.amtMinus, mx, my)) { st->graph->set_mapping_amount(d, std::max(0.f, maps[i].amount - 0.1f)); return; }
                if (hit(rc.amtPlus, mx, my))  { st->graph->set_mapping_amount(d, std::min(4.f, maps[i].amount + 0.1f)); return; }
                if (hit(rc.curMinus, mx, my)) { st->graph->set_mapping_curve(d, std::max(-1.f, maps[i].curve - 0.25f)); return; }
                if (hit(rc.curPlus, mx, my))  { st->graph->set_mapping_curve(d, std::min(1.f, maps[i].curve + 0.25f)); return; }
                if (hit(rc.loMinus, mx, my))  { st->graph->set_mapping_lo(d, std::max(0.f, maps[i].out_lo - 0.1f)); return; }
                if (hit(rc.loPlus, mx, my))   { st->graph->set_mapping_lo(d, std::min(1.f, maps[i].out_lo + 0.1f)); return; }
                if (hit(rc.hiMinus, mx, my))  { st->graph->set_mapping_hi(d, std::max(0.f, maps[i].out_hi - 0.1f)); return; }
                if (hit(rc.hiPlus, mx, my))   { st->graph->set_mapping_hi(d, std::min(1.f, maps[i].out_hi + 0.1f)); return; }
                if (hit(rc.clear, mx, my))    { st->graph->disconnect_dest(d); return; }
                break;
            }
            return;  // click inside the panel: consume
        }
        g_show_mappings = false; return;  // click outside: close
    }

    // Clip editor is non-modal: route presses inside its panel to it; clicks
    // elsewhere pass through to the session. A release always ends any editor drag.
    if (st->editor && st->editor->is_open()) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) st->editor->on_up(mx, my);
        if (action == GLFW_PRESS && st->editor->contains(mx, my)) {
            if (button == GLFW_MOUSE_BUTTON_LEFT) st->editor->on_down(mx, my, glfwGetTime());
            return;
        }
    }

    // DAW | visuals splitter.
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) { st->split_drag = false; st->dock_drag = false; }
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && hit(dock_resize_rect(), mx, my)) {
        st->dock_drag = true; return;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && hit(splitter_rect(), mx, my)) {
        const double now = glfwGetTime();
        if (now - st->split_last_t < 0.35) { g_split_x = std::round(g_win_w * 0.46f); st->split_drag = false; st->split_last_t = -1.0; }
        else { st->split_drag = true; st->split_last_t = now; }
        return;
    }

    // Right-click a meter (master or per-track) -> open its characteristic menu.
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        const int src = st->session ? meter_hit(tracks, scenes, mx, my) : -2;
        if (src != -2) st->menu = { true, static_cast<float>(mx), static_cast<float>(my), src };
        else st->menu.open = false;
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    if (action == GLFW_RELEASE) { st->gain_drag = -1; st->param_drag = -1; if (st->graph) st->graph->on_up(mx, my); return; }
    if (action != GLFW_PRESS) return;

    // Menu has priority: pick a characteristic -> spawn a data node in the graph.
    if (st->menu.open) {
        for (int j = 0; j < kNumChars; ++j) {
            const Rect r = { st->menu.x, st->menu.y + j * 26.f, 184.f, 26.f };
            if (hit(r, mx, my) && st->graph) {
                const int src = st->menu.src;
                const char* sname = src < 0 ? "Master" : vivid_poc::session_track_name(st->session, src);
                std::string title = std::string(sname) + "  " + kChars[j].label;
                st->graph->add_data_node(title, char_id_for(src, kChars[j].id));
                std::fprintf(stderr, "[vivid] bridge: spawned '%s %s' node\n", sname, kChars[j].label);
                break;
            }
        }
        st->menu.open = false;
        return;
    }
    // FX menu has priority: pick an effect -> add it to the menu's track.
    if (st->fx_menu.open) {
        for (int j = 0; j < vivid_poc::session_available_effect_count(); ++j) {
            const Rect r = { st->fx_menu.x, st->fx_menu.y + j * 24.f, 150.f, 24.f };
            if (hit(r, mx, my)) { vivid_poc::session_add_effect_by_index(st->session, st->fx_menu.src, j); break; }
        }
        st->fx_menu.open = false;
        return;
    }
    // Map menu: pick a source to drive the selected param (the return path).
    if (st->map_menu.open) {
        const int seltr = std::min(std::max(st->sel_track, 0), tracks - 1);
        const int seldev = std::max(0, st->sel_device);
        for (int j = 0; j < kNumMapSources; ++j) {
            const Rect rr = { st->map_menu.x, st->map_menu.y + j * 24.f, 168.f, 24.f };
            if (hit(rr, mx, my) && st->graph) {
                const std::string d = param_dest(seltr, seldev, st->map_param);
                if (kMapSources[j].id[0] == '\0') st->graph->disconnect_dest(d);
                else st->graph->add_mapping(kMapSources[j].id, d, 1.0f);
                break;
            }
        }
        st->map_menu.open = false;
        return;
    }
    if (!st->session) return;

    // A meter (master or per-track) -> open its characteristic menu (left-click).
    {
        const int src = meter_hit(tracks, scenes, mx, my);
        if (src != -2) { st->menu = { true, static_cast<float>(mx), static_cast<float>(my), src }; return; }
    }
    // Click a track header -> select it (its device chain shows in the DAW pane).
    for (int t = 0; t < tracks; ++t)
        if (hit(track_header_rect(t), mx, my)) { st->sel_track = t; if (st->graph) st->graph->select_op(-1); return; }

    // Bottom dock interactions. If a visual node is selected, the dock is its
    // inspector: knobs edit the node's base param values (vertical drag).
    {
        const int selop = st->graph ? st->graph->selected_op() : -1;
        if (selop >= 0 && my >= dock_top()) {   // only consume clicks inside the dock
            const DockGeom d = dock_geom();
            const int pc = st->graph->op_param_count_at(selop);
            for (int i = 0; i < pc; ++i) {
                float cx, cy; dock_knob(i, d, cx, cy);
                if (std::hypot(mx - cx, my - cy) <= 16.0) {
                    st->param_drag = i; st->param_is_node = true;
                    st->param_drag_v0 = st->graph->op_param_base_at(selop, i);
                    st->param_drag_y0 = my; return;
                }
            }
            return;  // node inspector showing — consume dock clicks
        }
    }
    // Otherwise the dock is the selected track's device chain: single-click selects
    // (shows params), double-click opens the plugin editor; x removes; + FX adds.
    {
        const int seltr = std::min(std::max(st->sel_track, 0), tracks - 1);
        const int nfx = vivid_poc::session_effect_count(st->session, seltr);
        const double now = glfwGetTime();
        auto open_dev = [&](int dev) {
            auto* ctrl = static_cast<Steinberg::Vst::IEditController*>(
                dev == 0 ? vivid_poc::session_track_controller(st->session, seltr)
                         : vivid_poc::session_effect_controller(st->session, seltr, dev - 1));
            if (!ctrl) return;
            const char* nm = dev == 0 ? vivid_poc::session_track_name(st->session, seltr)
                                      : vivid_poc::session_effect_name(st->session, seltr, dev - 1);
            if (dev == 0) {
                if (st->track_win[seltr]) { vst3_plugin_window_close(st->track_win[seltr]); st->track_win[seltr] = nullptr; }
                st->track_win[seltr] = vst3_plugin_window_open(ctrl, nm);
            } else {
                int slot = -1; for (int k = 0; k < 8; ++k) if (!st->fx_win[k]) { slot = k; break; }
                if (slot >= 0) st->fx_win[slot] = vst3_plugin_window_open(ctrl, nm);
            }
        };
        auto click_dev = [&](int dev) {
            if (st->last_dev_i == dev && now - st->last_dev_t < 0.35) { open_dev(dev); st->last_dev_t = -1; }
            else { st->sel_device = dev; st->last_dev_i = dev; st->last_dev_t = now; }
        };
        // device chips in the bottom dock
        if (!vivid_poc::session_track_is_audio(st->session, seltr) && hit(dock_chip(0), mx, my)) { click_dev(0); return; }
        for (int e = 0; e < nfx; ++e) {
            if (hit(dock_chip_x(1 + e), mx, my)) {
                vivid_poc::session_remove_effect(st->session, seltr, e);
                if (st->sel_device > nfx - 1) st->sel_device = 0;
                return;
            }
            if (hit(dock_chip(1 + e), mx, my)) { click_dev(1 + e); return; }
        }
        if (hit(dock_chip(1 + nfx), mx, my)) {
            st->fx_menu = { true, static_cast<float>(mx), static_cast<float>(my), seltr };
            return;
        }
        // param knobs of the selected device (vertical drag; small map affordance)
        const int seldev = std::max(0, st->sel_device);
        const DockGeom d = dock_geom();
        const int npc = std::min(vivid_poc::session_param_count(st->session, seltr, seldev), d.cols * d.maxRows);
        for (int i = 0; i < npc; ++i) {
            if (hit(dock_knob_map(i, d), mx, my)) {
                st->map_menu = { true, static_cast<float>(mx), static_cast<float>(my), 0 };
                st->map_param = i; return;
            }
            float cx, cy; dock_knob(i, d, cx, cy);
            if (std::hypot(mx - cx, my - cy) <= 16.0) {
                st->param_drag = i; st->param_is_node = false;
                st->param_drag_v0 = vivid_poc::session_param_value(st->session, seltr, seldev, i);
                st->param_drag_y0 = my;
                return;
            }
        }
    }
    // mixer gain sliders
    for (int t = 0; t < tracks; ++t) {
        const Rect gr = track_gain_rect(t, scenes);
        if (hit(gr, mx, my)) {
            st->gain_drag = t;
            vivid_poc::session_set_track_gain(st->session, t, std::min(1.0, std::max(0.0, (mx - gr.x) / gr.w)));
            return;
        }
    }
    if (st->graph && st->graph->on_down(mx, my)) return;  // node graph consumed it
    // clip cells -> single click launches; double click opens the MIDI editor
    for (int t = 0; t < tracks; ++t)
        for (int sc = 0; sc < scenes; ++sc)
            if (hit(clip_cell_rect(t, sc), mx, my)) {
                const double now = glfwGetTime();
                if (st->editor && st->last_clip_track == t && st->last_clip_scene == sc && now - st->last_clip_t < 0.35) {
                    char title[80];
                    std::snprintf(title, sizeof title, "%s  \xC2\xB7  Clip %c",
                                  vivid_poc::session_track_name(st->session, t), 'A' + sc);
                    if (vivid_poc::session_track_is_audio(st->session, t)) {  // waveform editor
                        float bins[512]; float a = 0.f, b = 1.f;
                        const int nb = vivid_poc::session_audio_waveform(st->session, t, sc, bins, 512);
                        vivid_poc::session_get_audio_trim(st->session, t, sc, &a, &b);
                        st->editor->open_audio(t, sc, title, bins, nb, a, b);
                    } else {                                                  // piano-roll editor
                        vivid_poc::ClipNote buf[256];
                        const int n = vivid_poc::session_get_clip(st->session, t, sc, buf, 256);
                        const double len = vivid_poc::session_clip_length(st->session, t, sc);
                        st->editor->open(t, sc, title, buf, n, len);
                    }
                    st->last_clip_t = -1;
                    return;
                }
                st->last_clip_t = now; st->last_clip_track = t; st->last_clip_scene = sc;
                vivid_poc::session_launch_clip(st->session, t, sc);
                return;
            }
    // scene launch buttons -> launch the whole row
    for (int sc = 0; sc < scenes; ++sc)
        if (hit(scene_launch_rect(sc), mx, my)) { vivid_poc::session_launch_scene(st->session, sc); return; }
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
