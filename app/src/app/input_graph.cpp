#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window.h"
#include "ui/layout.h"
#include "ui/node_graph.h"          // NodeGraph::zoom_at
#include "ui/audio_node_graph.h"
#include "ui/audio_catalog.h"       // A3: the unified add catalog (native ops + VST3 + CLAP)
#include "ui/compound_widget.h"     // VIVID_DISPLAY_LFO (compound-widget hints)
#include "gpu/visual_graph.h"
#include "audio/vst3_host.h"
#include "audio/vst3_plugin_window.h"   // open a VST3 node's plugin editor from the graph
#include "app/operator_clone.h"     // clone_operator / operator_has_clone_template / CloneResult
#include "platform/platform.h"      // open_in_editor

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
using namespace vivid::ui;          // hit, Rect, AudioNodeGraph/Box, dock/audio_graph rects
namespace S = vivid::session;
}

namespace vivid::input {

// ---- A3: the audio graph's Tab chooser — the ONE way to add an audio node ----
// It offers the UNIFIED catalog (native operators + every installed VST3 + every installed CLAP);
// the old "+ Src"/"+ FX" menus could only ever list native ops, and the plugin browser could only
// ever list plugins, so neither could add everything.

// The track whose graph is on screen (the detail region shows exactly one).
static int audio_graph_track(const Window& win, App& app) {
    if (!app.session) return -1;
    const int n = S::session_track_count(app.session);
    if (n <= 0) return -1;
    return std::min(std::max(win.sel_track, 0), n - 1);
}

bool audio_chooser_open_at(Window& win, App& app, double mx, double my) {
    if (win.focus.kind != vivid::FocusContext::Kind::AudioGraph || my < win.dock_top()) return false;
    if (audio_graph_track(win, app) < 0) return false;
    const Rect gp = audio_graph_panel(win.win_w, win.win_h, win.dock_h);
    win.audio_chooser_new_track = false;
    win.audio_chooser.set_entries(vivid::ui::audio_catalog(app.session));
    // Keep the panel inside the window (the dock is short, so it opens UPWARD from the dock top).
    win.audio_chooser.show(mx, my, 8.f, vivid::ui::kTopBarH + 8.f,
                           static_cast<float>(win.win_w) - 8.f, gp.y + gp.h);
    return true;
}

// "+ Track": the same chooser, filtered to things that can START a signal (native instruments,
// VST3 + CLAP instruments). Picking one creates the track and puts it in.
void audio_chooser_open_new_track(Window& win, App& app, double mx, double my) {
    if (!app.session) return;
    win.audio_chooser_new_track = true;
    win.audio_chooser.set_entries(vivid::ui::audio_catalog(app.session, /*instruments_only*/true));
    win.audio_chooser.show(mx, my, 8.f, vivid::ui::kTopBarH + 8.f,
                           static_cast<float>(win.win_w) - 8.f,
                           static_cast<float>(win.win_h) - 8.f);
}

// Spawn what the chooser handed back. Every path lands in the authoritative graph.
static void audio_chooser_spawn(Window& win, App& app, const vivid::ui::Chooser::Entry& e) {
    int tr = audio_graph_track(win, app);
    if (win.audio_chooser_new_track) {   // "+ Track": make the track, then put the instrument in it
        win.audio_chooser_new_track = false;
        tr = S::session_add_graph_track(app.session, "");   // a bare track; the chooser supplies the source
        if (tr < 0) return;
        win.sel_track = tr;
        if (app.graph) app.graph->select_op(-1);            // focus the new track's audio graph
    }
    if (tr < 0) return;
    namespace U = vivid::ui;
    int nid = -1;
    switch (e.tag) {
        case U::kAudioNativeEffect: nid = S::session_audio_graph_add_op(app.session, tr, e.id.c_str()); break;
        case U::kAudioNativeSource: nid = S::session_audio_graph_add_source(app.session, tr, e.id.c_str()); break;
        case U::kAudioPluginEffect:
        case U::kAudioPluginSource: {
            const bool src = (e.tag == U::kAudioPluginSource);
            const bool clap = e.badge == "CLAP";
            nid = S::session_audio_graph_add_plugin(app.session, tr, e.id.c_str(),
                                                    clap ? S::kFmtCLAP : S::kFmtVST3, src ? 1 : 0, "");
            break;
        }
        default: break;
    }
    if (nid >= 0) win.sel_audio_node = nid;   // select what you just made
    else std::fprintf(stderr, "[vivid] could not add '%s' to track %d\n", e.label.c_str(), tr);
}

// Keys while the audio chooser owns the keyboard. Returns true when consumed.
bool audio_chooser_key(Window& win, App& app, int key) {
    if (!win.audio_chooser.open()) return false;
    if (key == GLFW_KEY_ESCAPE) { win.audio_chooser.hide(); return true; }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        if (const auto* e = win.audio_chooser.confirm()) audio_chooser_spawn(win, app, *e);
        return true;
    }
    if (key == GLFW_KEY_DOWN || key == GLFW_KEY_TAB) { win.audio_chooser.move(+1); return true; }
    if (key == GLFW_KEY_UP)        { win.audio_chooser.move(-1); return true; }
    if (key == GLFW_KEY_BACKSPACE) { win.audio_chooser.backspace(); return true; }
    return true;   // swallow everything else while it's up
}

bool audio_chooser_char(Window& win, unsigned int cp) {
    if (!win.audio_chooser.open()) return false;
    win.audio_chooser.type(cp);
    return true;
}

// A click while the chooser is up: pick a row, or dismiss. Consumes either way.
bool audio_chooser_click(Window& win, App& app, double mx, double my) {
    if (!win.audio_chooser.open()) return false;
    bool dismissed = false;
    if (const auto* e = win.audio_chooser.click(mx, my, dismissed)) audio_chooser_spawn(win, app, *e);
    return true;
}

// Scroll-wheel zoom for whichever graph is under the cursor: the visuals node graph (when the
// graph deep-view is revealed, right of the splitter) and/or the audio-graph deep-view (2i, zoom
// around the cursor). Neither "consumes" the scroll (matches the original fall-through order), so
// this returns void and is called last in scroll_callback.
void graph_scroll(Window& win, App& app, double yoff, double mx, double my) {
    // Visuals column: zoom the node graph around the cursor (the graph owns the column, ADR-0014).
    if (app.graph && mx >= win.split_x && my < win.dock_top())
        app.graph->zoom_at(mx, my, std::pow(1.12f, static_cast<float>(yoff)));
    // Audio-graph deep view: zoom around the cursor (keeps the point under the cursor fixed).
    if (win.focus.kind == vivid::FocusContext::Kind::AudioGraph && app.session) {
        vivid::ui::AudioNodeGraph ag; ag.set_source(app.session, win.sel_track);
        const vivid::ui::Rect gp = vivid::ui::audio_graph_panel(win.win_w, win.win_h, win.dock_h);
        ag.set_bounds(gp.x, gp.y, gp.x + gp.w, gp.y + gp.h);
        ag.set_selection(win.sel_audio_node);   // match draw's band height for the zoom hit-region
        const vivid::ui::Rect gr = ag.graph_region();
        if (mx >= gr.x && mx < gr.x + gr.w && my >= gr.y && my < gr.y + gr.h) {
            const float z0 = win.ag_zoom;
            const float z1 = std::clamp(z0 * std::pow(1.12f, static_cast<float>(yoff)), 0.35f, 4.0f);
            win.ag_pan_x = static_cast<float>(mx) - gr.x - ((static_cast<float>(mx) - gr.x - win.ag_pan_x) / z0) * z1;
            win.ag_pan_y = static_cast<float>(my) - gr.y - ((static_cast<float>(my) - gr.y - win.ag_pan_y) / z0) * z1;
            win.ag_zoom = z1;
        }
    }
}

// UI-3 audio node graph deep view (press). Drill in via the Device header "Graph" button; the
// close x returns to the device chain. While drilled in: + FX opens the native-effect picker,
// select a node, drag its param knobs (by node id), remove an effect x, start a rewire from an
// output port, click an edge to disconnect, double-click empty to reset the view else pan. All
// dock clicks are consumed here (returns true) so they never reach the device-chip handlers.
bool graph_audio_dock(Window& win, App& app, int button, int action, double mx, double my) {
    if (!(win.focus.kind == vivid::FocusContext::Kind::AudioGraph && my >= win.dock_top()
          && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && app.session)) return false;
    const int tr = std::min(std::max(win.sel_track, 0), S::session_track_count(app.session) - 1);
    AudioNodeGraph ag; ag.set_source(app.session, tr);
    const Rect gp = audio_graph_panel(win.win_w, win.win_h, win.dock_h);
    ag.set_bounds(gp.x, gp.y, gp.x + gp.w, gp.y + gp.h);
    ag.set_selection(win.sel_audio_node);   // size the param band as draw does (compound preview)
    // (The "+ Src" / "+ FX" buttons are gone: they could only ever offer NATIVE ops — plugins were
    // structurally excluded — so no surface could add everything. Tab is now the one add path, over
    // the unified catalog. See audio_chooser_open_at below.)
    if (win.sel_audio_node >= 0 && ag.sel_is_source(win.sel_audio_node)) {   // key-range drag handles (source node)
        int lo = 0, hi = 127;
        S::session_audio_graph_node_key_range_get(app.session, tr, win.sel_audio_node, &lo, &hi);
        if (hit(ag.key_lo_rect(win.sel_audio_node), mx, my)) { win.ag_key_drag = 0; win.ag_key_v0 = lo; win.ag_key_y0 = my; return true; }
        if (hit(ag.key_hi_rect(win.sel_audio_node), mx, my)) { win.ag_key_drag = 1; win.ag_key_v0 = hi; win.ag_key_y0 = my; return true; }
    }
    if (win.sel_audio_node >= 0) {   // param knob drag on the selected node (by node id)
        for (const auto& c : ag.param_cells(win.sel_audio_node)) {
            // The map dot (top-right of the cell) takes priority over the knob rect it sits inside:
            // open the bridge map-source picker for this node param (dock_menus emits a "gnode:" dest).
            // The clickable area is padded larger than the drawn dot (a 10px dot is a hard target) but
            // stays clear of the knob to its left.
            const Rect dd = ag_param_map_dot(c);
            if (hit(Rect{ dd.x - 4.f, dd.y - 2.f, dd.w + 8.f, dd.h + 9.f }, mx, my)) {
                // The dot is in the param band at the dock's bottom, so open the whole picker ABOVE the
                // dock: rows overlapping dock_top would be stolen by the dock-resize handle (handled
                // before dock_menus). Clamp x too (no right spill), and keep it below the top bar.
                const float menu_w = 168.f, item_h = 24.f, marg = 8.f, menu_h = kNumMapSources * item_h;
                const float fx = std::min(static_cast<float>(mx), win.win_w - menu_w - marg);
                const float fy = std::max(marg + 22.f, std::min(static_cast<float>(my), win.dock_top() - menu_h - marg));
                win.map_menu = { true, fx, fy, win.sel_audio_node, true /*graph*/ };
                win.map_param = c.index;
                return true;
            }
            if (mx >= c.x && mx < c.x + c.w && my >= c.y && my < c.y + c.h) {
                const float mn = S::session_audio_graph_node_param_min(app.session, tr, win.sel_audio_node, c.index);
                const float mxx = S::session_audio_graph_node_param_max(app.session, tr, win.sel_audio_node, c.index);
                const float v = S::session_audio_graph_node_param_get(app.session, tr, win.sel_audio_node, c.index);
                win.ag_param_drag = c.index;
                win.ag_param_v0 = (mxx > mn) ? std::clamp((v - mn) / (mxx - mn), 0.f, 1.f) : 0.f;
                win.ag_param_y0 = my; return true;
            }
        }
        // UI-4a: clicking the LFO waveform preview cycles the enum (wraps min..max).
        for (const auto& cp : ag.compound_previews()) {
            if (cp.hint != VIVID_DISPLAY_LFO || !hit(cp.rect, mx, my)) continue;
            const float mn = S::session_audio_graph_node_param_min(app.session, tr, win.sel_audio_node, cp.index);
            const float mxx = S::session_audio_graph_node_param_max(app.session, tr, win.sel_audio_node, cp.index);
            const float v = S::session_audio_graph_node_param_get(app.session, tr, win.sel_audio_node, cp.index);
            const float next = (v >= mxx - 0.5f) ? mn : v + 1.f;   // integer enum step, wrap at max
            S::session_audio_graph_node_param_set(app.session, tr, win.sel_audio_node, cp.index, next);
            return true;
        }
    }
    const auto boxes = ag.layout();
    for (const auto& b : boxes)   // start a rewire drag from an output port (release connects)
        if (b.kind != 2 && hit(ag.out_port_rect(b), mx, my)) { win.ag_wire_from = b.node_id; return true; }
    for (const auto& b : boxes) {   // remove-x (effects) or select — both by node id
        if (b.kind == 1 && hit(ag.remove_rect(b), mx, my)) {
            S::session_audio_graph_remove_node(app.session, tr, b.node_id);
            if (win.sel_audio_node == b.node_id) win.sel_audio_node = vivid::Window::kNoAudioNode;
            return true;
        }
        if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
            win.sel_audio_node = (b.kind == 2) ? vivid::Window::kNoAudioNode : b.node_id;   // output has no params
            // Double-click a VST3 node → open its native plugin editor (replaces the old chip double-click).
            const double now = glfwGetTime();
            if (win.ag_last_node == b.node_id && now - win.ag_last_node_t < 0.35) {
                if (auto* ctrl = static_cast<Steinberg::Vst::IEditController*>(
                        S::session_audio_graph_node_controller(app.session, tr, b.node_id))) {
                    int slot = -1; for (int k = 0; k < vivid::session::kMaxTracks; ++k) if (!win.fx_win[k]) { slot = k; break; }
                    if (slot >= 0) win.fx_win[slot] = vst3_plugin_window_open(ctrl, S::session_track_name(app.session, tr));
                }
                win.ag_last_node_t = -1;
            } else { win.ag_last_node = b.node_id; win.ag_last_node_t = now; }
            win.ag_node_drag = b.node_id;                                   // start a reposition drag (any node)
            win.ag_node_dx = (mx - b.x) / win.ag_zoom;                      // grab offset in world units
            win.ag_node_dy = (my - b.y) / win.ag_zoom;
            return true;
        }
    }
    // Click an edge (in the empty space between cards) to disconnect it.
    auto box_of = [&](int nid) -> const AudioNodeBox* {
        for (const auto& b : boxes) if (b.node_id == nid) return &b; return nullptr; };
    const int ne = S::session_track_audio_graph_edge_count(app.session, tr);
    for (int e = 0; e < ne; ++e) {
        const AudioNodeBox* a = box_of(S::session_track_audio_graph_edge_from(app.session, tr, e));
        const AudioNodeBox* b = box_of(S::session_track_audio_graph_edge_to(app.session, tr, e));
        if (!a || !b) continue;
        const float ax = a->x + a->w, ay = a->y + a->h * 0.5f, bx = b->x, by = b->y + b->h * 0.5f;
        const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
        float t = (l2 > 0.f) ? ((mx - ax) * dx + (my - ay) * dy) / l2 : 0.f;
        t = std::clamp(t, 0.f, 1.f);
        const float px = ax + t * dx, py = ay + t * dy;
        if ((mx - px) * (mx - px) + (my - py) * (my - py) < 36.f) {   // within ~6px of the edge
            S::session_audio_graph_disconnect(app.session, tr, a->node_id, b->node_id);
            return true;
        }
    }
    // Empty space: double-click resets the view (2i); otherwise start a pan drag.
    const double now = glfwGetTime();
    if (now - win.ag_last_click_t < 0.30) {
        win.ag_zoom = 1.f; win.ag_pan_x = win.ag_pan_y = 0.f; win.ag_last_click_t = -1; return true;
    }
    win.ag_last_click_t = now;
    win.ag_panning = true; win.ag_pan_mx0 = mx; win.ag_pan_my0 = my;
    win.ag_pan_ox0 = win.ag_pan_x; win.ag_pan_oy0 = win.ag_pan_y;
    return true;   // consume other clicks in the graph
}

// Right-click a visuals op node -> open its context menu (Open source / Clone & Edit).
bool graph_node_rclick(Window& win, App& app, int button, int action, double mx, double my) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS && app.graph && mx >= win.split_x) {
        const int on = app.graph->op_at(mx, my);
        if (on >= 0) {
            win.node_menu = { true, static_cast<float>(mx), static_cast<float>(my), on,
                              !app.graph->op_source_path(on).empty(),
                              vivid::operator_has_clone_template(app.graph->op_kind_name(on)) };
            win.menu.open = false;
            return true;
        }
    }
    return false;
}

// Complete an audio-graph rewire: a release over another node's input port connects the edge.
// Returns true when a rewire drag was in progress (consumes the release).
bool graph_rewire_release(Window& win, App& app, double mx, double my) {
    if (win.ag_wire_from >= 0 && app.session) {
        const int tr = std::min(std::max(win.sel_track, 0), S::session_track_count(app.session) - 1);
        AudioNodeGraph ag; ag.set_source(app.session, tr);
        const Rect gp = audio_graph_panel(win.win_w, win.win_h, win.dock_h);
        ag.set_bounds(gp.x, gp.y, gp.x + gp.w, gp.y + gp.h);
        for (const auto& b : ag.layout())
            if (b.kind != 0 && b.node_id != win.ag_wire_from && hit(ag.in_port_rect(b), mx, my)) {
                S::session_audio_graph_connect(app.session, tr, win.ag_wire_from, b.node_id);
                break;
            }
        win.ag_wire_from = -1;
        return true;
    }
    return false;
}

// Node context menu press: "Open source" (custom nodes) or "Clone & Edit" (built-ins). Returns
// true when the menu was open (it always closes + consumes the click).
bool graph_nodemenu(Window& win, App& app, double mx, double my) {
    if (!win.node_menu.open) return false;
    const int nn = win.node_menu.node;
    if (app.graph && hit(Rect{ win.node_menu.x, win.node_menu.y, 172.f, 22.f }, mx, my)) {
        if (win.node_menu.has_source) {
            const std::string src = app.graph->op_source_path(nn);
            if (!src.empty()) vivid::platform::open_in_editor(src);
        } else if (win.node_menu.cloneable) {
            vivid::CloneResult cr = vivid::clone_operator(app.op_registry, app.op_loaders, app.graph->op_kind_name(nn));
            if (cr.ok) { app.graph->swap_op_type(nn, cr.name); vivid::platform::open_in_editor(cr.source_path); }
            else std::fprintf(stderr, "[vivid] clone failed: %s\n", cr.error.c_str());
        }
    }
    win.node_menu.open = false;
    return true;
}

}  // namespace vivid::input
