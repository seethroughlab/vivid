#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window.h"
#include "ui/layout.h"
#include "ui/session_view.h"     // DevSlot, dock_resolve, dock_param_dest, dock_device_count, meter_hit
#include "ui/node_graph.h"       // add_data_node / add_mapping / disconnect_dest
#include "audio/vst3_host.h"

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
    for (int j = 0; j < kNumChars; ++j) {
        const Rect r = { win.menu.x, win.menu.y + j * 26.f, 184.f, 26.f };
        if (hit(r, mx, my) && app.graph) {
            const int src = win.menu.src;   // -1 master, else a session track index
            const char* sname = src < 0 ? "Master" : S::session_track_name(app.session, src);
            const int sid = src < 0 ? -1 : S::session_track_id(app.session, src);
            std::string title = std::string(sname) + "  " + kChars[j].label;
            app.graph->add_data_node(title, char_id_for(sid, kChars[j].id));
            std::fprintf(stderr, "[vivid] bridge: spawned '%s %s' node\n", sname, kChars[j].label);
            break;
        }
    }
    win.menu.open = false;
    return true;
}

// The three device/menu pickers, in priority order. Each: if open, dispatch the click to a row
// then close + consume. FX menu (graph mode = native effects via the graph edit API; device mode
// = the VST3 catalog first, then native operators — matches draw_fx_menu ordering), the +Track
// instrument picker, and the mapping-source picker (the return path).
bool dock_menus(Window& win, App& app, double mx, double my, int tracks) {
    if (win.fx_menu.open) {
        const int nvst = win.fx_menu.graph ? 0 : S::session_available_effect_count();
        const int nnat = S::session_available_audio_op_count(app.session, 0);
        for (int j = 0; j < nvst + nnat; ++j) {
            const Rect r = { win.fx_menu.x, win.fx_menu.y + j * 24.f, 150.f, 24.f };
            if (hit(r, mx, my)) {
                if (j < nvst) S::session_add_effect_by_index(app.session, win.fx_menu.src, j);
                else {
                    const char* op = S::session_available_audio_op_name(app.session, 0, j - nvst);
                    if (win.fx_menu.graph) S::session_audio_graph_add_op(app.session, win.fx_menu.src, op);
                    else                   S::session_add_audio_effect(app.session, win.fx_menu.src, op);
                }
                break;
            }
        }
        win.fx_menu.open = false;
        return true;
    }
    if (win.track_menu.open) {
        const int n = S::session_available_instrument_count();
        for (int j = 0; j <= n; ++j) {
            const Rect r = { win.track_menu.x, win.track_menu.y + j * 24.f, 150.f, 24.f };
            if (hit(r, mx, my)) {
                if (j == n) S::session_add_audio_track(app.session);
                else        S::session_add_instrument_track(app.session, S::session_available_instrument_name(j));
                break;
            }
        }
        win.track_menu.open = false;
        return true;
    }
    if (win.map_menu.open) {
        const int seltr = std::min(std::max(win.sel_track, 0), tracks - 1);
        const DevSlot seldev = dock_resolve(app.session, seltr, std::max(0, win.sel_device));
        for (int j = 0; j < kNumMapSources; ++j) {
            const Rect rr = { win.map_menu.x, win.map_menu.y + j * 24.f, 168.f, 24.f };
            if (hit(rr, mx, my) && app.graph) {
                const std::string d = dock_param_dest(seltr, seldev, win.map_param);
                if (kMapSources[j].id[0] == '\0') app.graph->disconnect_dest(d);
                else app.graph->add_mapping(kMapSources[j].id, d, 1.0f);
                break;
            }
        }
        win.map_menu.open = false;
        return true;
    }
    return false;
}

}  // namespace vivid::input
