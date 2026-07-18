#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/edit_gateway.h"   // ADR-0017/G3 note_edit
#include "app/window.h"
#include "ui/layout.h"
#include "ui/node_graph.h"          // NodeGraph::zoom_at
#include "ui/audio_node_graph.h"
#include "ui/audio_catalog.h"       // A3: the unified add catalog (native ops + VST3 + CLAP)
#include "ui/compound_widget.h"     // VIVID_DISPLAY_LFO (compound-widget hints)
#include "ui/param_widget.h"        // Phase 2b: NodeWidget (curated inspector row widgets)
#include "gpu/visual_graph.h"
#include "audio/vst3_host.h"
#include "audio/vst3_plugin_window.h"   // open a VST3 node's plugin editor from the graph
#include "app/operator_clone.h"     // clone_operator / operator_has_clone_template / CloneResult
#include "packages/package_manifest.h"  // parse_package_manifest (ADR-0020: watch the fresh clone)
#include "gpu/shader_library.h"         // ADR-0020: shader entries/tier + fork (fork-to-edit)
#include "ui/shader_library_view.h"     // shader_fork_name
#include <filesystem>
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

// ADR-0022: the modulation shape editor popover. Reads the edited control edge's current shape
// (scans for the (from,node,param) edge), applies a change through set_control_shape + note_edit.
static bool mod_editor_read(Window& win, App& app, float& amount, float& curve, int& invert, int& bipolar) {
    const int tr = audio_graph_track(win, app);
    if (tr < 0) return false;
    const ModEditor& m = win.mod_editor;
    for (int e = 0, ne = S::session_track_audio_graph_edge_count(app.session, tr); e < ne; ++e) {
        if (S::session_track_audio_graph_edge_kind(app.session, tr, e) != 2) continue;
        if (S::session_track_audio_graph_edge_from(app.session, tr, e) != m.from) continue;
        if (S::session_track_audio_graph_edge_to(app.session, tr, e) != m.node) continue;
        if (S::session_track_audio_graph_edge_dest_param(app.session, tr, e) != m.param) continue;
        S::session_track_audio_graph_edge_control_shape(app.session, tr, e, &amount, &curve, &invert, &bipolar);
        return true;
    }
    return false;
}
static void mod_editor_apply(Window& win, App& app, float amount, float curve, int invert, int bipolar) {
    const int tr = audio_graph_track(win, app);
    if (tr < 0) return;
    const ModEditor& m = win.mod_editor;
    S::session_audio_graph_set_control_shape(app.session, tr, m.from, m.node, m.param, amount, curve, invert, bipolar);
    if (app.edit_gateway) app.edit_gateway->note_edit("Shape Modulation", "mod_shape");   // coalesce a drag
}

// Press handler for the open editor. Returns true (consumed) whenever the editor is open — a click
// anywhere while it's up is either a control interaction or a dismiss.
bool mod_editor_press(Window& win, App& app, double mx, double my) {
    if (!win.mod_editor.open || !app.session) return false;
    const ModEditor& m = win.mod_editor;
    float amount = 1.f, curve = 0.f; int invert = 0, bipolar = 0;
    if (!mod_editor_read(win, app, amount, curve, invert, bipolar)) { win.mod_editor.open = false; return false; }
    if (!hit(mod_editor_panel(m), mx, my)) { win.mod_editor.open = false; return true; }   // click-away closes
    const Rect aw = mod_editor_widget(m, 0), cw = mod_editor_widget(m, 1);
    if (hit(aw, mx, my)) { win.mod_ed_drag = 0; mod_editor_apply(win, app, std::clamp((float)(mx - aw.x) / aw.w, 0.f, 1.f), curve, invert, bipolar); return true; }
    if (hit(cw, mx, my)) { win.mod_ed_drag = 1; mod_editor_apply(win, app, amount, std::clamp((float)(mx - cw.x) / cw.w, 0.f, 1.f) * 2.f - 1.f, invert, bipolar); return true; }
    if (hit(mod_editor_row(m, 2), mx, my)) { mod_editor_apply(win, app, amount, curve, invert, bipolar ? 0 : 1); return true; }
    if (hit(mod_editor_row(m, 3), mx, my)) { mod_editor_apply(win, app, amount, curve, invert ? 0 : 1, bipolar); return true; }
    if (hit(mod_editor_row(m, 4), mx, my)) {   // remove the modulation
        const int tr = audio_graph_track(win, app);
        S::session_audio_graph_disconnect_control(app.session, tr, m.from, m.node, m.param);
        if (app.edit_gateway) app.edit_gateway->note_edit("Disconnect Modulation", "");
        win.mod_editor.open = false;
        return true;
    }
    return true;   // inside the panel but not on a control — consume (don't fall through to the graph)
}

// Continue a slider drag (called from the frame loop while the button is held).
void mod_editor_drag(Window& win, App& app, double mx, double /*my*/) {
    if (win.mod_ed_drag < 0 || !win.mod_editor.open) return;
    float amount = 1.f, curve = 0.f; int invert = 0, bipolar = 0;
    if (!mod_editor_read(win, app, amount, curve, invert, bipolar)) return;
    const Rect w = mod_editor_widget(win.mod_editor, win.mod_ed_drag);
    const float v = std::clamp((float)(mx - w.x) / w.w, 0.f, 1.f);
    if (win.mod_ed_drag == 0) mod_editor_apply(win, app, v, curve, invert, bipolar);
    else                      mod_editor_apply(win, app, amount, v * 2.f - 1.f, invert, bipolar);
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
        case U::kAudioNoteOp:       nid = S::session_audio_graph_add_note_op(app.session, tr, e.id.c_str()); break;
        case U::kAudioModOp:        nid = S::session_audio_graph_add_mod_op(app.session, tr, e.id.c_str()); break;
        case U::kAudioMidiIn:       nid = S::session_audio_graph_add_midi_in(app.session, tr); break;
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
    if (nid >= 0) { win.sel_audio_node = nid;   // select what you just made
        if (app.edit_gateway) app.edit_gateway->note_edit("Add Audio Node", "");   // ADR-0017/G3
    } else std::fprintf(stderr, "[vivid] could not add '%s' to track %d\n", e.label.c_str(), tr);
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

// --- Phase 2c: the curated inspector's "+ Add param" palette (the Tab chooser, reused). Its entries
// are the selected plugin node's UNPINNED params; each carries its param index in `tag`. ---
static void param_chooser_show_at(Window& win, App& app, double mx, double my) {
    const Rect gp = audio_graph_panel(win.win_w, win.win_h, win.dock_h);
    win.param_chooser.show(mx, my, 8.f, vivid::ui::kTopBarH + 8.f,
                           static_cast<float>(win.win_w) - 8.f, gp.y + gp.h);
}

// Action 0: pick an UNPINNED param to add to the curated inspector (entry tag = param index).
void param_chooser_open(Window& win, App& app, int node, double mx, double my) {
    const int tr = audio_graph_track(win, app);
    if (tr < 0 || node < 0) return;
    std::vector<vivid::ui::ChooserEntry> entries;
    const int pc = S::session_audio_graph_node_param_count(app.session, tr, node);
    for (int p = 0; p < pc; ++p) {
        if (S::session_audio_graph_node_param_is_pinned(app.session, tr, node, p)) continue;
        vivid::ui::ChooserEntry e;
        e.label = S::session_audio_graph_node_param_name(app.session, tr, node, p);
        e.tag   = p;
        if (const char* d = S::session_audio_graph_node_param_display(app.session, tr, node, p); d && *d) e.summary = d;
        entries.push_back(std::move(e));
    }
    win.param_chooser_node = node; win.param_chooser_action = 0;
    win.param_chooser.set_entries(std::move(entries));
    param_chooser_show_at(win, app, mx, my);
}

// Action 1: pick a value for an enum param (entry tag = choice index) — a real dropdown list in
// place of click-cycle. Same palette (type-to-filter/scroll), which handles long enums.
void param_enum_chooser_open(Window& win, App& app, int node, int param, double mx, double my) {
    const int tr = audio_graph_track(win, app);
    if (tr < 0 || node < 0) return;
    const int cc = S::session_audio_graph_node_param_count(app.session, tr, node) ?
                   S::session_audio_graph_node_param_choice_count(app.session, tr, node, param) : 0;
    if (cc <= 0) return;
    std::vector<vivid::ui::ChooserEntry> entries;
    for (int k = 0; k < cc; ++k) {
        vivid::ui::ChooserEntry e;
        const char* lbl = S::session_audio_graph_node_param_choice_label(app.session, tr, node, param, k);
        e.label = (lbl && *lbl) ? lbl : std::to_string(k);
        e.tag   = k;
        entries.push_back(std::move(e));
    }
    win.param_chooser_node = node; win.param_chooser_param = param; win.param_chooser_action = 1;
    win.param_chooser.set_entries(std::move(entries));
    param_chooser_show_at(win, app, mx, my);
}

// Apply the confirmed entry per the current action, then close (reopen to pick again).
static void param_chooser_confirm(Window& win, App& app, const vivid::ui::ChooserEntry& e) {
    const int tr = audio_graph_track(win, app);
    if (tr >= 0) {
        if (win.param_chooser_action == 1) {   // set the enum param to choice e.tag's value
            const int cc = S::session_audio_graph_node_param_choice_count(app.session, tr, win.param_chooser_node, win.param_chooser_param);
            const float mn = S::session_audio_graph_node_param_min(app.session, tr, win.param_chooser_node, win.param_chooser_param);
            const float mx = S::session_audio_graph_node_param_max(app.session, tr, win.param_chooser_node, win.param_chooser_param);
            const float v = (cc > 1) ? mn + static_cast<float>(e.tag) / static_cast<float>(cc - 1) * (mx - mn) : mn;
            S::session_audio_graph_node_param_set(app.session, tr, win.param_chooser_node, win.param_chooser_param, v);
            if (app.edit_gateway) app.edit_gateway->note_edit("Set Param", "");   // ADR-0017/G3
        } else {                                // add (pin) param e.tag
            S::session_audio_graph_node_param_pin(app.session, tr, win.param_chooser_node, e.tag);
            if (app.edit_gateway) app.edit_gateway->note_edit("Pin Param", "");   // ADR-0017/G3
        }
    }
    win.param_chooser.hide();
}

bool param_chooser_key(Window& win, App& app, int key) {
    if (!win.param_chooser.open()) return false;
    if (key == GLFW_KEY_ESCAPE) { win.param_chooser.hide(); return true; }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        if (const auto* e = win.param_chooser.confirm()) param_chooser_confirm(win, app, *e);
        return true;
    }
    if (key == GLFW_KEY_DOWN || key == GLFW_KEY_TAB) { win.param_chooser.move(+1); return true; }
    if (key == GLFW_KEY_UP)        { win.param_chooser.move(-1); return true; }
    if (key == GLFW_KEY_BACKSPACE) { win.param_chooser.backspace(); return true; }
    return true;
}

bool param_chooser_char(Window& win, unsigned int cp) {
    if (!win.param_chooser.open()) return false;
    win.param_chooser.type(cp);
    return true;
}

bool param_chooser_click(Window& win, App& app, double mx, double my) {
    if (!win.param_chooser.open()) return false;
    bool dismissed = false;
    if (const auto* e = win.param_chooser.click(mx, my, dismissed)) param_chooser_confirm(win, app, *e);
    else if (dismissed) win.param_chooser.hide();
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
        vivid::ui::AudioNodeGraph& ag = *app.audio_graph; ag.set_source(app.session, win.sel_track);
        const vivid::ui::Rect gp = vivid::ui::audio_graph_panel(win.win_w, win.win_h, win.dock_h);
        ag.set_bounds(gp.x, gp.y, gp.x + gp.w, gp.y + gp.h);
        ag.set_selection(win.sel_audio_node);   // match draw's band height for the zoom hit-region
        const vivid::ui::Rect gr = ag.graph_region();
        if (mx >= gr.x && mx < gr.x + gr.w && my >= gr.y && my < gr.y + gr.h) {
            // Keep the world point under the cursor fixed across the zoom. World-under-cursor via the
            // shared transform; the zoom clamp ([0.35,4.0]) is the audio graph's own policy.
            const vivid::ui::NodeView v0 = vivid::ui::region_view(gr, win.ag_zoom, win.ag_pan_x, win.ag_pan_y);
            double wx, wy; v0.to_world(mx, my, wx, wy);
            const float z1 = std::clamp(win.ag_zoom * std::pow(1.12f, static_cast<float>(yoff)), 0.35f, 4.0f);
            win.ag_pan_x = static_cast<float>(mx) - gr.x - static_cast<float>(wx) * z1;
            win.ag_pan_y = static_cast<float>(my) - gr.y - static_cast<float>(wy) * z1;
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
    AudioNodeGraph& ag = *app.audio_graph; ag.set_source(app.session, tr);
    const Rect gp = audio_graph_panel(win.win_w, win.win_h, win.dock_h);
    ag.set_bounds(gp.x, gp.y, gp.x + gp.w, gp.y + gp.h);
    ag.set_view(win.ag_zoom, win.ag_pan_x, win.ag_pan_y);   // MUST match the draw, or hit-tests miss when panned
    ag.set_selection(win.sel_audio_node);   // size the param band as draw does (compound preview)
    // "Editor" button in the dock header → open the selected VST3 node's native plugin window (its
    // full param surface). Mirrors the node double-click open path; shown only when it has a controller.
    if (win.sel_audio_node >= 0
        && hit(vivid::ui::dock_audio_editor_button_rect(win.win_w, win.win_h, win.dock_h), mx, my)) {
        if (auto* ctrl = static_cast<Steinberg::Vst::IEditController*>(
                S::session_audio_graph_node_controller(app.session, tr, win.sel_audio_node))) {
            int slot = -1; for (int k = 0; k < vivid::session::kMaxTracks; ++k) if (!win.fx_win[k]) { slot = k; break; }
            if (slot >= 0) win.fx_win[slot] = vst3_plugin_window_open(ctrl, S::session_track_name(app.session, tr));
        }
        return true;
    }
    // (The "+ Src" / "+ FX" buttons are gone: they could only ever offer NATIVE ops — plugins were
    // structurally excluded — so no surface could add everything. Tab is now the one add path, over
    // the unified catalog. See audio_chooser_open_at below.)
    if (win.sel_audio_node >= 0 && ag.sel_is_source(win.sel_audio_node)) {   // key-range drag handles (source node)
        int lo = 0, hi = 127;
        S::session_audio_graph_node_key_range_get(app.session, tr, win.sel_audio_node, &lo, &hi);
        if (hit(ag.key_lo_rect(win.sel_audio_node), mx, my)) { win.ag_key_drag = 0; win.ag_key_v0 = lo; win.ag_key_y0 = my; return true; }
        if (hit(ag.key_hi_rect(win.sel_audio_node), mx, my)) { win.ag_key_drag = 1; win.ag_key_v0 = hi; win.ag_key_y0 = my; return true; }
    }
    // Curated inspector (Phase 2b): a plugin node's pinned params are vertical rows, not a knob grid.
    if (win.sel_audio_node >= 0 && ag.is_plugin_node(win.sel_audio_node)) {
        if (hit(ag.add_param_button_rect(win.sel_audio_node), mx, my)) {   // open the searchable "+ Add param" palette
            param_chooser_open(win, app, win.sel_audio_node, mx, my);
            return true;
        }
        for (const auto& row : ag.pinned_rows(win.sel_audio_node)) {
            // bridge map dot (same picker as the knob strip) — opened above the dock
            if (hit(Rect{ row.mapdot.x - 4.f, row.mapdot.y - 2.f, row.mapdot.w + 8.f, row.mapdot.h + 8.f }, mx, my)) {
                const float menu_w = 168.f, item_h = 24.f, marg = 8.f, menu_h = kNumMapSources * item_h;
                const float fx = std::min(static_cast<float>(mx), win.win_w - menu_w - marg);
                const float fy = std::max(marg + 22.f, std::min(static_cast<float>(my), win.dock_top() - menu_h - marg));
                win.map_menu = { true, fx, fy, win.sel_audio_node };
                win.map_param = row.index;
                return true;
            }
            if (hit(row.remove, mx, my)) {   // × → unpin (remove from the curated set)
                S::session_audio_graph_node_param_unpin(app.session, tr, win.sel_audio_node, row.index);
                if (app.edit_gateway) app.edit_gateway->note_edit("Unpin Param", "");   // ADR-0017/G3
                return true;
            }
            if (hit(row.row, mx, my)) {
                const float mn = S::session_audio_graph_node_param_min(app.session, tr, win.sel_audio_node, row.index);
                const float mxx = S::session_audio_graph_node_param_max(app.session, tr, win.sel_audio_node, row.index);
                const float v = S::session_audio_graph_node_param_get(app.session, tr, win.sel_audio_node, row.index);
                const vivid::ui::NodeWidget wk = static_cast<vivid::ui::NodeWidget>(row.widget);
                if (wk == vivid::ui::NodeWidget::Toggle) {   // click flips
                    const float mid = mn + (mxx - mn) * 0.5f;
                    S::session_audio_graph_node_param_set(app.session, tr, win.sel_audio_node, row.index, v > mid ? mn : mxx);
                    if (app.edit_gateway) app.edit_gateway->note_edit("Toggle Param", "");   // ADR-0017/G3
                    return true;
                }
                if (wk == vivid::ui::NodeWidget::Enum) {   // open the choice list (a real dropdown, not click-cycle)
                    param_enum_chooser_open(win, app, win.sel_audio_node, row.index, mx, my);
                    return true;
                }
                // slider: start a HORIZONTAL drag and apply immediately at the click x
                win.ag_param_drag = row.index; win.ag_param_horiz = true;
                win.ag_param_rx = row.widget_rect.x; win.ag_param_rw = row.widget_rect.w;
                const float norm = std::clamp(static_cast<float>(mx - row.widget_rect.x) / row.widget_rect.w, 0.f, 1.f);
                S::session_audio_graph_node_param_set(app.session, tr, win.sel_audio_node, row.index, mn + norm * (mxx - mn));
                return true;
            }
        }
        // a click in the band that hit no row falls through to node select (the band is below the nodes)
    }
    if (win.sel_audio_node >= 0 && !ag.is_plugin_node(win.sel_audio_node)) {   // native knob strip (by node id)
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
                win.map_menu = { true, fx, fy, win.sel_audio_node };   // src = the audio-graph node id
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
            if (app.edit_gateway) app.edit_gateway->note_edit("Set Param", "");   // ADR-0017/G3
            return true;
        }
    }
    const auto boxes = ag.layout();
    for (const auto& b : boxes)   // start a rewire drag from an output port (release connects)
        if (b.kind != 2 && hit(ag.out_port_rect(b), mx, my)) { win.ag_wire_from = b.node_id; return true; }
    // ADR-0022: the "+ param" row on a plugin card opens the searchable picker to expose one more
    // param as a port (the curated set drives which plugin params show).
    for (const auto& b : boxes)
        if (hit(ag.add_param_port_rect(b), mx, my)) {
            win.sel_audio_node = b.node_id;
            param_chooser_open(win, app, b.node_id, mx, my);
            return true;
        }
    // ADR-0022: clicking a WIRED (magenta) param port opens the modulation shape editor for its
    // control edge. (An unwired port is a drag target only — a lone click there does nothing.)
    for (const auto& b : boxes) {
        const int slot = ag.param_port_hit(b, mx, my);
        if (slot < 0) continue;
        const std::vector<int> exp = ag.exposed_params(b.node_id);
        if (slot >= static_cast<int>(exp.size())) continue;
        const int param = exp[slot];
        int from = -1;   // the modulator driving this param (first control edge into it)
        for (int e = 0, ne = S::session_track_audio_graph_edge_count(app.session, tr); e < ne; ++e)
            if (S::session_track_audio_graph_edge_kind(app.session, tr, e) == 2
                && S::session_track_audio_graph_edge_to(app.session, tr, e) == b.node_id
                && S::session_track_audio_graph_edge_dest_param(app.session, tr, e) == param) {
                from = S::session_track_audio_graph_edge_from(app.session, tr, e); break;
            }
        if (from < 0) return true;   // an unwired port — consume the click, no editor
        const float px = std::min(static_cast<float>(mx), win.win_w - vivid::ui::kModEdW - 8.f);
        win.mod_editor = { true, px, static_cast<float>(my), b.node_id, param, from };
        win.sel_audio_node = b.node_id;
        return true;
    }
    for (const auto& b : boxes) {   // remove-x (effects) or select — both by node id
        if (b.kind == 1 && hit(ag.remove_rect(b), mx, my)) {
            S::session_audio_graph_remove_node(app.session, tr, b.node_id);
            if (win.sel_audio_node == b.node_id) win.sel_audio_node = vivid::Window::kNoAudioNode;
            if (app.edit_gateway) app.edit_gateway->note_edit("Remove Audio Node", "");   // ADR-0017/G3
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
            if (app.edit_gateway) app.edit_gateway->note_edit("Disconnect Audio", "");   // ADR-0017/G3
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
            // ADR-0020: resolve ONE contextual edit action for this node.
            const std::string op_type = app.graph->op_type_at(on);
            NodeMenu m; m.open = true; m.x = static_cast<float>(mx); m.y = static_cast<float>(my); m.node = on;
            std::string shader_path, shader_tier;
            for (const auto& e : app.shader_library.entries())
                if (e.name == op_type) { shader_path = e.path; shader_tier = e.tier; break; }
            std::string watched, asset;
            if (shader_tier == "bundled") {          // shipped shader is read-only → fork a copy to edit
                m.action = NodeMenu::Action::ForkEdit; m.target = op_type;
            } else if (!shader_tier.empty()) {       // user/project shader → open its .wgsl (watcher reloads)
                m.action = NodeMenu::Action::OpenSource; m.target = shader_path;
            } else if (watched = app.hot_reload.source_for(op_type), !watched.empty()) {
                m.action = NodeMenu::Action::OpenSource; m.target = watched;   // cloned/user C++ op source
            } else if (asset = app.graph->op_source_path(on), !asset.empty()) {
                m.action = NodeMenu::Action::OpenSource; m.target = asset;      // CustomShader .glsl asset
            } else if (vivid::operator_has_clone_template(app.graph->op_kind_name(on))) {
                m.action = NodeMenu::Action::CloneEdit;                          // built-in → clone to edit
            }
            win.node_menu = m;
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
        AudioNodeGraph& ag = *app.audio_graph; ag.set_source(app.session, tr);
        const Rect gp = audio_graph_panel(win.win_w, win.win_h, win.dock_h);
        ag.set_bounds(gp.x, gp.y, gp.x + gp.w, gp.y + gp.h);
        ag.set_view(win.ag_zoom, win.ag_pan_x, win.ag_pan_y);   // MUST match the draw, or hit-tests miss when panned
        // ADR-0015: what SIGNAL the dragged wire carries follows from what the source node emits.
        // A MidiIn (kind 3) and a note effect (kind 4) emit notes and nothing else, so a wire out of
        // them is a NOTE edge; everything else is audio. (An explicit port picker can come later —
        // this is unambiguous today because no node emits both.)
        int from_kind = -1;
        for (const auto& b : ag.layout()) if (b.node_id == win.ag_wire_from) { from_kind = b.kind; break; }
        const bool note_wire = (from_kind == 3 || from_kind == 4);
        // ADR-0022: a modulator (kind 5) emits CONTROL, which targets a specific PARAM. A wire from
        // it lands on a param PORT (exposed down a node's left edge), creating a control edge to that
        // exact param — the drag gesture P0.5 deferred, now driven by the mouse.
        if (from_kind == 5) {
            for (const auto& b : ag.layout()) {
                if (b.node_id == win.ag_wire_from) continue;
                const int slot = ag.param_port_hit(b, mx, my);
                if (slot < 0) continue;
                const std::vector<int> exp = ag.exposed_params(b.node_id);
                if (slot >= static_cast<int>(exp.size())) continue;
                // Default shape: full-range unipolar sweep (amount 1, curve 0). A shape editor
                // (amount/bipolar) is the natural next step; a sensible default keeps the drop useful.
                if (S::session_audio_graph_connect_control(app.session, tr, win.ag_wire_from, b.node_id,
                                                           exp[slot], /*amount*/1.f, /*curve*/0.f,
                                                           /*invert*/0, /*bipolar*/0)
                    && app.edit_gateway)
                    app.edit_gateway->note_edit("Connect Modulation", "");
                break;
            }
            win.ag_wire_from = -1;
            return true;
        }
        for (const auto& b : ag.layout()) {
            if (b.node_id == win.ag_wire_from || !hit(ag.in_port_rect(b), mx, my)) continue;
            // A note wire may land on an instrument (kind 0) or another note effect; an audio wire
            // may not land on a source.
            if (note_wire) {
                if (b.kind == 0 || b.kind == 4) {
                    S::session_audio_graph_connect_kind(app.session, tr, win.ag_wire_from, b.node_id, 1);
                    if (app.edit_gateway) app.edit_gateway->note_edit("Connect Audio", "");   // ADR-0017/G3
                }
            } else if (b.kind != 0 && b.kind != 3 && b.kind != 4) {
                S::session_audio_graph_connect_kind(app.session, tr, win.ag_wire_from, b.node_id, 0);
                if (app.edit_gateway) app.edit_gateway->note_edit("Connect Audio", "");
            }
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
        switch (win.node_menu.action) {
            case NodeMenu::Action::OpenSource:
                if (!win.node_menu.target.empty()) vivid::platform::open_in_editor(win.node_menu.target);
                break;
            case NodeMenu::Action::ForkEdit: {
                // ADR-0020: fork a shipped (read-only) shader into the user tier, swap the node to
                // the copy, and open it — the always-on shader watcher live-updates saved edits.
                const std::string& base = win.node_menu.target;
                auto& reg = app.op_registry;
                const std::string nn2 = vivid::ui::shader_fork_name(base, [&](const std::string& c) { return reg.has(c); });
                std::string err;
                const std::string forked = app.shader_library.fork(base, nn2, reg, err);
                if (!forked.empty()) {
                    app.graph->swap_op_type(nn, forked);
                    for (const auto& e : app.shader_library.entries())
                        if (e.name == forked) { vivid::platform::open_in_editor(e.path); break; }
                    VLOG_INFO(app, "forked shader '%s' -> '%s' (editing your copy)", base.c_str(), forked.c_str());
                } else VLOG_ERR(app, "fork shader '%s' failed: %s", base.c_str(), err.c_str());
                break;
            }
            case NodeMenu::Action::CloneEdit: {
                vivid::CloneResult cr = vivid::clone_operator(app.op_registry, app.op_loaders, app.graph->op_kind_name(nn));
                if (cr.ok) {
                    app.graph->swap_op_type(nn, cr.name);
                    // ADR-0020 W2: watch the fresh clone so editing its source reloads live (no restart).
                    const std::string pkgdir = std::filesystem::path(cr.source_path).parent_path().string();
                    app.hot_reload.watch_manifest(app.op_loaders, vivid::parse_package_manifest(pkgdir));
                    vivid::platform::open_in_editor(cr.source_path);
                    VLOG_INFO(app, "cloned '%s' -> '%s' (editing your copy)", app.graph->op_kind_name(nn), cr.name.c_str());
                } else VLOG_ERR(app, "clone failed: %s", cr.error.c_str());
                break;
            }
            case NodeMenu::Action::None: break;
        }
    }
    win.node_menu.open = false;
    return true;
}

}  // namespace vivid::input
