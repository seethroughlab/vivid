// UX Ph4 F3 (keyboard editing): drive the two node graphs from the keyboard, so building a patch never
// requires the mouse. Acts on the FOCUSED graph — a selected visual op means the visual graph, else the
// selected track's audio graph (mirrors the FocusContext derivation in frame.cpp). The mouse paths are
// pixel/port hit-test driven; this bypasses that by calling the same index/id-addressable model APIs
// (NodeGraph::set_op_input_at / delete_op, session_audio_graph_connect_kind / _remove_node / _disconnect).
//
// Bindings (all gated to PRESS/REPEAT + !win.typing by the caller; Esc-cancel is handled in input.cpp):
//   [ / ]  cycle selection    arrows  spatial select (nearest node in that direction)
//   \      switch visual<->audio pane          Delete / Backspace  delete the selected node
//   W      start a wire from the selection's output, then W again on a target to commit
//   , / .  cycle the target input port (visual multi-input)     Shift+Backspace  disconnect an input
#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window.h"
#include "app/edit_gateway.h"
#include "app/kbd_edit_logic.h"   // pure, unit-tested wire-kind inference + spatial nearest
#include "ui/node_graph.h"
#include "audio/vst3_host.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace vivid::input {
namespace S = vivid::session;
namespace {

enum class Graph { Visual, Audio };

// A node in the active graph, unified for nav: `idx` addresses it in that graph (visual: op index;
// audio: enumeration index), `id` is its stable id, (x,y) its canvas position, `kind` its role.
struct KNode { int idx; int id; float x; float y; int kind; };

Graph active_graph(App& app) {
    return (app.graph && app.graph->selected_op() >= 0) ? Graph::Visual : Graph::Audio;
}

int audio_track(const Window& win, App& app) {
    if (!app.session) return -1;
    const int n = S::session_track_count(app.session);
    if (n <= 0) return -1;
    return std::min(std::max(win.sel_track, 0), n - 1);
}

std::vector<KNode> enum_nodes(const Window& win, App& app, Graph g) {
    std::vector<KNode> out;
    if (g == Graph::Visual && app.graph) {
        for (int i = 0, n = app.graph->op_count(); i < n; ++i) {
            int in = -1, id = 0; float x = 0.f, y = 0.f; app.graph->get_op(i, in, id, x, y);
            out.push_back({ i, id, x, y, 0 });
        }
    } else if (g == Graph::Audio && app.session) {
        const int tr = audio_track(win, app); if (tr < 0) return out;
        for (int i = 0, n = S::session_track_audio_graph_node_count(app.session, tr); i < n; ++i) {
            const int id = S::session_track_audio_graph_node_id(app.session, tr, i);
            float x = 0.f, y = 0.f; S::session_track_audio_graph_node_pos(app.session, tr, i, &x, &y);
            out.push_back({ i, id, x, y, S::session_track_audio_graph_node_kind(app.session, tr, i) });
        }
    }
    return out;
}

// Index within `nodes` of the current selection, or -1.
int sel_index(const Window& win, App& app, Graph g, const std::vector<KNode>& nodes) {
    if (g == Graph::Visual) {
        const int s = app.graph ? app.graph->selected_op() : -1;
        for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].idx == s) return static_cast<int>(i);
    } else {
        for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].id == win.sel_audio_node) return static_cast<int>(i);
    }
    return -1;
}

void select(Window& win, App& app, Graph g, const KNode& n) {
    if (g == Graph::Visual) { if (app.graph) app.graph->select_op(n.idx); }
    else win.sel_audio_node = n.id;
}

// [ / ] — cycle the selection (wrapping). With no selection, land on the first (]) or last ([).
bool cycle(Window& win, App& app, Graph g, int dir) {
    const auto nodes = enum_nodes(win, app, g);
    if (nodes.empty()) return true;   // consume, nothing to do
    const int cur = sel_index(win, app, g, nodes);
    const int n = static_cast<int>(nodes.size());
    const int nxt = (cur < 0) ? (dir > 0 ? 0 : n - 1) : ((cur + dir) % n + n) % n;
    select(win, app, g, nodes[nxt]);
    return true;
}

// Arrows — move the selection to the nearest node in the given screen direction (dx,dy).
bool spatial(Window& win, App& app, Graph g, float dx, float dy) {
    const auto nodes = enum_nodes(win, app, g);
    if (nodes.empty()) return true;
    const int cur = sel_index(win, app, g, nodes);
    if (cur < 0) { select(win, app, g, nodes[0]); return true; }   // nothing selected yet → pick one
    std::vector<float> xs, ys; xs.reserve(nodes.size()); ys.reserve(nodes.size());
    for (const auto& n : nodes) { xs.push_back(n.x); ys.push_back(n.y); }
    const int best = nearest_in_dir(xs, ys, cur, dx, dy);
    if (best >= 0) select(win, app, g, nodes[best]);
    return true;
}

// \ — move keyboard focus between the two graphs. Visual focus == a non-negative selected_op, so
// switching is just moving the selection into the other graph (and priming a node there).
bool switch_pane(Window& win, App& app) {
    if (active_graph(app) == Graph::Visual) {
        if (app.graph) app.graph->select_op(-1);              // -> audio focus
        const auto a = enum_nodes(win, app, Graph::Audio);
        if (!a.empty() && sel_index(win, app, Graph::Audio, a) < 0) win.sel_audio_node = a.front().id;
    } else {
        const auto v = enum_nodes(win, app, Graph::Visual);   // -> visual focus
        if (!v.empty()) app.graph->select_op(v.front().idx);
    }
    return true;
}

// Delete / Backspace — remove the selected node (guards live in the model APIs).
bool del(Window& win, App& app, Graph g) {
    if (g == Graph::Visual) {
        if (!app.graph) return true;
        app.graph->delete_op(app.graph->selected_op());       // no-op on Output / out-of-range
        return true;
    }
    const int tr = audio_track(win, app);
    if (tr < 0 || win.sel_audio_node == Window::kNoAudioNode) return true;
    if (S::session_audio_graph_remove_node(app.session, tr, win.sel_audio_node)) {   // effects-only (API-guarded)
        win.sel_audio_node = Window::kNoAudioNode;
        if (app.edit_gateway) app.edit_gateway->note_edit("Remove Audio Node", "");
    }
    return true;
}

// W — start a wire from the selection's output, or (if one is pending) commit it to the selection.
bool wire(Window& win, App& app, Graph g) {
    if (win.kbd_wire_dom == 0) {   // START
        if (g == Graph::Visual) {
            const int i = app.graph ? app.graph->selected_op() : -1;
            if (i < 0) return true;
            win.kbd_wire_dom = 1; win.kbd_wire_from = i; win.kbd_wire_port = 0;
        } else {
            const auto nodes = enum_nodes(win, app, g);
            const int si = sel_index(win, app, g, nodes);
            if (si < 0) return true;
            if (!audio_can_source(nodes[si].kind)) return true;   // only inst/fx/note-fx/modulator can source
            win.kbd_wire_dom = 2; win.kbd_wire_from = nodes[si].id; win.kbd_wire_port = 0;
        }
        return true;
    }
    // COMMIT — the wire domain must match the graph now focused.
    if (g == Graph::Visual && win.kbd_wire_dom == 1 && app.graph) {
        const int tgt = app.graph->selected_op(), src = win.kbd_wire_from;
        if (tgt >= 0 && tgt != src) {
            const int ports = std::max(1, app.graph->op_input_port_count(tgt));
            app.graph->set_op_input_at(tgt, std::min(win.kbd_wire_port, ports - 1), src, 0);
            app.graph->note_edit("Wire Nodes");
        }
    } else if (g == Graph::Audio && win.kbd_wire_dom == 2) {
        const int tr = audio_track(win, app);
        const auto nodes = enum_nodes(win, app, g);
        const int si = sel_index(win, app, g, nodes);
        if (tr >= 0 && si >= 0 && nodes[si].id != win.kbd_wire_from) {
            int from_kind = -1;
            for (const auto& n : nodes) if (n.id == win.kbd_wire_from) { from_kind = n.kind; break; }
            const int wk = audio_wire_kind(from_kind, nodes[si].kind);   // 1 note / 0 audio / -1 illegal
            // (A modulator source, wk==-1 from kind 5, needs a param-port pick — left to the mouse / Mappings.)
            if (wk >= 0
                && S::session_audio_graph_connect_kind(app.session, tr, win.kbd_wire_from, nodes[si].id, wk)
                && app.edit_gateway) app.edit_gateway->note_edit("Connect Audio", "");
        }
    }
    win.kbd_wire_dom = 0; win.kbd_wire_from = -1; win.kbd_wire_port = 0;
    return true;
}

// Shift+Backspace — disconnect an input edge of the selected node.
bool disconnect(Window& win, App& app, Graph g) {
    if (g == Graph::Visual && app.graph) {
        const int i = app.graph->selected_op();
        if (i < 0) return true;
        const auto ins = app.graph->op_inputs_at(i);          // current texture-input edges (by port)
        for (int p = 0; p < static_cast<int>(ins.size()); ++p)
            if (ins[p] >= 0) { app.graph->set_op_input_at(i, p, -1); app.graph->note_edit("Disconnect"); break; }
        return true;
    }
    const int tr = audio_track(win, app);
    if (tr < 0 || win.sel_audio_node == Window::kNoAudioNode) return true;
    for (int e = 0, n = S::session_track_audio_graph_edge_count(app.session, tr); e < n; ++e) {
        if (S::session_track_audio_graph_edge_to(app.session, tr, e) != win.sel_audio_node) continue;
        const int from = S::session_track_audio_graph_edge_from(app.session, tr, e);
        if (S::session_track_audio_graph_edge_kind(app.session, tr, e) == 2)   // control edge -> a param dest
            S::session_audio_graph_disconnect_control(app.session, tr, from, win.sel_audio_node,
                                                      S::session_track_audio_graph_edge_dest_param(app.session, tr, e));
        else
            S::session_audio_graph_disconnect(app.session, tr, from, win.sel_audio_node);
        if (app.edit_gateway) app.edit_gateway->note_edit("Disconnect Audio", "");
        break;
    }
    return true;
}

}  // namespace

bool kbd_edit_key(Window& win, App& app, int key, int mods) {
    const Graph g = active_graph(app);
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    switch (key) {
        case GLFW_KEY_RIGHT_BRACKET: return cycle(win, app, g, +1);
        case GLFW_KEY_LEFT_BRACKET:  return cycle(win, app, g, -1);
        case GLFW_KEY_RIGHT: return spatial(win, app, g, +1.f, 0.f);
        case GLFW_KEY_LEFT:  return spatial(win, app, g, -1.f, 0.f);
        case GLFW_KEY_DOWN:  return spatial(win, app, g, 0.f, +1.f);
        case GLFW_KEY_UP:    return spatial(win, app, g, 0.f, -1.f);
        case GLFW_KEY_BACKSLASH: return switch_pane(win, app);
        case GLFW_KEY_W:     return wire(win, app, g);
        case GLFW_KEY_COMMA:  if (win.kbd_wire_dom) { win.kbd_wire_port = std::max(0, win.kbd_wire_port - 1); return true; } return false;
        case GLFW_KEY_PERIOD: if (win.kbd_wire_dom) { win.kbd_wire_port = std::min(3, win.kbd_wire_port + 1); return true; } return false;
        case GLFW_KEY_DELETE: return del(win, app, g);
        case GLFW_KEY_BACKSPACE: return shift ? disconnect(win, app, g) : del(win, app, g);
        default: return false;
    }
}

}  // namespace vivid::input
