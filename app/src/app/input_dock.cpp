#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/edit_gateway.h"   // ADR-0017 note_edit (one-shot param widgets)
#include "app/window.h"
#include "ui/layout.h"
#include "ui/compound_widget.h"  // UI-4a: is_compound_widget / compound_span / xy_from_cursor
#include "ui/session_view.h"     // DevSlot, dock_resolve, dock_param_dest, dock_device_count, meter_hit
#include "ui/node_graph.h"       // add_data_node / add_mapping / disconnect_dest
#include "audio/vst3_host.h"
#include "audio/vst3_plugin_window.h"   // vst3_plugin_window_open/close + Steinberg::Vst::IEditController
#include "platform/file_dialog.h"       // open_file_dialog (Image node path picker)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
using namespace vivid::ui;    // hit/Rect, kChars/char_id_for, kMapSources, node widget helpers, dock_* + meter_hit
namespace S = vivid::session;
}

namespace vivid::input {

// Characteristics context menu (right-click a meter): pick a characteristic -> spawn a bridge data
// node in the visuals graph, encoded by the track's STABLE id so the wire follows the track.
bool dock_char_menu(Window& win, App& app, double mx, double my) {
    if (!win.menu.open) return false;
    const int row = win.menu.hit_row(static_cast<float>(mx), static_cast<float>(my));
    if (row >= 0 && app.graph) {
        const int src = win.menu.a;   // -1 master, else a session track index (ADR-0027 payload)
        const char* sname = src < 0 ? "Master" : S::session_track_name(app.session, src);
        const int sid = src < 0 ? -1 : S::session_track_id(app.session, src);
        const int cj = win.menu.items[row].id;   // index into kChars
        std::string title = std::string(sname) + "  " + kChars[cj].label;
        app.graph->add_data_node(title, char_id_for(sid, kChars[cj].id));
        std::fprintf(stderr, "[vivid] bridge: spawned '%s %s' node\n", sname, kChars[cj].label);
    }
    win.menu.close();
    return true;
}

// Click in the audio-node "→ visuals" menu: spawn a bridge data-node in the VISUALS graph sourced from
// this audio node (node_<track-stable-id>_<node>.<rms|fft.k>). Cross-surface, exactly like dock_char_menu.
bool audio_node_menu_click(Window& win, App& app, double mx, double my) {
    if (!win.audio_node_menu.open) return false;
    const int track = win.audio_node_menu.a, node = win.audio_node_menu.b;   // ADR-0027 payload
    const int row = win.audio_node_menu.hit_row(static_cast<float>(mx), static_cast<float>(my));
    if (row >= 0 && app.graph && app.session) {
        // re-resolve op type (label) + is-modulator to pick the catalog the row indexes into
        const char* nm = "node"; bool ismod = false;
        const int nn = S::session_track_audio_graph_node_count(app.session, track);
        for (int i = 0; i < nn; ++i)
            if (S::session_track_audio_graph_node_id(app.session, track, i) == node) {
                const char* t = S::session_track_audio_graph_node_type(app.session, track, i);
                if (t && *t) nm = t;
                ismod = S::session_track_audio_graph_node_kind(app.session, track, i) == 5;
                break;
            }
        const AudioNodeChar* items = ismod ? kModNodeChars : kAudioNodeChars;
        const int j = win.audio_node_menu.items[row].id;   // index into the chosen catalog
        const int tid = S::session_track_id(app.session, track);
        const std::string src = "node_" + std::to_string(tid) + "_" + std::to_string(node) + "." + items[j].suffix;
        app.graph->add_data_node(std::string(nm) + " " + items[j].label, src);
        std::fprintf(stderr, "[vivid] bridge: spawned '%s %s' -> %s\n", nm, items[j].label, src.c_str());
    }
    win.audio_node_menu.close();
    return true;
}

// The mapping-source picker (the bridge return path) — the only dock menu left. The "+ FX"/"+ Src"
// pickers and the "+ Track" instrument menu are gone: adding anything is the Tab chooser now.
bool dock_menus(Window& win, App& app, double mx, double my, int tracks) {
    if (win.map_menu.open) {
        const int seltr = std::min(std::max(win.sel_track, 0), tracks - 1);
        const int row = win.map_menu.hit_row(static_cast<float>(mx), static_cast<float>(my));
        if (row >= 0 && app.graph) {
            // The map dot is only ever opened from an audio-graph node, so the dest addresses that node
            // by its STABLE id ("gnode:"). (The linear device-param dest went away with the device
            // chain.) map_menu.a = the node id; the item id indexes kMapSources.
            const int mj = win.map_menu.items[row].id;
            const std::string d = gnode_param_dest(seltr, win.map_menu.a, win.map_param);
            if (kMapSources[mj].id[0] == '\0') app.graph->disconnect_dest(d);
            else app.graph->add_mapping(kMapSources[mj].id, d, 1.0f);
        }
        win.map_menu.close();
        return true;
    }
    return false;
}

// Visual-node inspector (UI-1): when a visuals op node is selected, the dock is its inspector —
// knobs/sliders/toggles/enums edit the node's base param values (routed through the explicit
// focus). Consumes all dock clicks while showing. Returns true iff the inspector is up.
bool dock_inspector(Window& win, App& app, double mx, double my) {
    if (!(win.focus.kind == vivid::FocusContext::Kind::VisualNode && app.graph && my >= win.dock_top())) return false;
    auto* g = app.graph;
    const int selop = win.focus.node;
    const int pc = g->op_param_count_at(selop);
    for (int i = 0; i < pc; ++i) {
        const int hint = g->op_param_hint_at(selop, i);
        if (hint == VIVID_DISPLAY_HIDDEN || hint == VIVID_DISPLAY_EDITOR || hint == VIVID_DISPLAY_TRANSIENT) continue;
        // UI-4a: a compound widget claims several params; hit its spanning rect, then start its drag.
        if (is_compound_widget(hint)) {
            const int span = compound_span(hint);
            const Rect cr = node_param_compound_rect(i, span, win.win_w, win.win_h, win.dock_h);
            if (hit(cr, mx, my)) {
                if (hint == VIVID_DISPLAY_XY_PAD) {   // set both axes from the cursor + start an XY drag
                    float x01, y01; xy_from_cursor(cr, mx, my, x01, y01);
                    g->set_op_param_base_at(selop, i, x01);
                    g->set_op_param_base_at(selop, i + 1, y01);
                    win.param_drag = i; win.param_is_node = true; win.param_xy = true;
                } else if (hint == VIVID_DISPLAY_COLOR) {   // a channel slider = an ordinary horizontal drag
                    for (int k = 0; k < 3; ++k) {
                        const Rect wr = node_param_widget_rect(i + k, win.win_w, win.win_h, win.dock_h);
                        if (hit(wr, mx, my)) {
                            g->set_op_param_base_at(selop, i + k, std::clamp((mx - wr.x) / wr.w, 0.0, 1.0));
                            win.param_drag = i + k; win.param_is_node = true; win.param_drag_horiz = true; win.param_xy = false;
                            win.param_drag_v0 = 0.f; win.param_drag_y0 = my;
                            break;
                        }
                    }
                }
                return true;
            }
            i += span - 1;
            continue;
        }
        const Rect wr = node_param_widget_rect(i, win.win_w, win.win_h, win.dock_h);
        if (!hit(wr, mx, my)) continue;
        const float base = g->op_param_base_at(selop, i);
        switch (node_widget_kind(g->op_param_type_at(selop, i), hint, g->op_param_choice_count_at(selop, i))) {
            case NodeWidget::Toggle:
                g->set_op_param_base_at(selop, i, base >= 0.5f ? 0.f : 1.f);
                if (win.app->edit_gateway) win.app->edit_gateway->note_edit("Toggle Param", "");
                break;
            case NodeWidget::Enum: {
                const int cc = g->op_param_choice_count_at(selop, i);
                if (cc > 1) { int idx = (int(std::lround(base * (cc - 1))) + 1) % cc; g->set_op_param_base_at(selop, i, float(idx) / (cc - 1)); }
                if (win.app->edit_gateway) win.app->edit_gateway->note_edit("Set Param", "");
                break;
            }
            case NodeWidget::File: {   // open a native file chooser (filtered to the op's types); set the path
                const auto exts = win.app->file_drops.extensions_for_op(g->op_type_at(selop));  // ADR-0021/P3
                const std::string path = vivid::platform::open_file_dialog("Choose a file", exts);
                if (!path.empty()) { g->set_op_file_param_at(selop, i, path);
                    if (win.app->edit_gateway) win.app->edit_gateway->note_edit("Set File", ""); }  // ADR-0017/G3
                break;
            }
            case NodeWidget::Slider:
                g->set_op_param_base_at(selop, i, std::clamp((mx - wr.x) / wr.w, 0.0, 1.0));   // jump to click
                win.param_drag = i; win.param_is_node = true; win.param_drag_horiz = true; win.param_xy = false;
                win.param_drag_v0 = 0.f; win.param_drag_y0 = my; break;
            default:  // Knob: vertical drag
                win.param_drag = i; win.param_is_node = true; win.param_drag_horiz = false; win.param_xy = false;
                win.param_drag_v0 = base; win.param_drag_y0 = my; break;
        }
        return true;
    }
    return true;  // node inspector showing — consume dock clicks
}

// (dock_device_chain — the linear device chip-row input — was retired; the audio node graph
// is a track's primary detail view. Its editing lives in AudioNodeGraph::on_down/on_move/on_up.)

}  // namespace vivid::input
