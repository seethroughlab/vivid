#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstring>

#include "app/app.h"
#include "app/edit_gateway.h"   // ADR-0017/G3 note_edit
#include "app/window.h"
#include "ui/layout.h"
#include "ui/node_graph.h"          // NodeGraph::zoom_at
#include "ui/audio_node_graph.h"
#include "ui/audio_catalog.h"       // A3: the unified add catalog (native ops + VST3 + CLAP)
#include "ui/operator_draw_bridge.h" // ADR-0050: DrawBridge + make_op_draw_api (CATALOG preview swatch)
#include "ui/compound_widget.h"     // VIVID_DISPLAY_LFO (compound-widget hints)
#include "ui/param_widget.h"        // Phase 2b: NodeWidget (curated inspector row widgets)
#include "gpu/visual_graph.h"
#include "audio/vst3_host.h"
#include "audio/vst3_plugin_window.h"   // open a VST3 node's plugin editor from the graph
#include "audio/clap_plugin_window.h"   // open a CLAP node's plugin editor (clap.gui) from the graph
#include "app/operator_clone.h"     // clone_operator / operator_has_clone_template / CloneResult
#include "packages/package_manifest.h"  // parse_package_manifest (ADR-0020: watch the fresh clone)
#include "gpu/shader_library.h"         // ADR-0020: shader entries/tier + fork (fork-to-edit)
#include "ui/shader_library_view.h"     // shader_fork_name
#include <filesystem>
#include "platform/platform.h"      // open_in_editor

#include <algorithm>
#include <cstdlib>   // std::atoi (audio param-menu track tag)
#include <cmath>
#include <cstdio>
#include <string>

namespace {
using namespace vivid::ui;          // hit, Rect, AudioNodeGraph/Box, dock/audio_graph rects
namespace S = vivid::session;

// The audio editor now spans two panes: the node CANVAS below the session view, and the selected
// node's param strip in the bottom dock. A press belongs to the audio editor if it lands in EITHER —
// used to gate the gesture handlers (they no longer live only in the dock band).
inline bool ag_pane_hit(const vivid::Window& win, vivid::App& app, double mx, double my) {
    if (!app.session) return false;
    const int scenes = S::session_scene_count(app.session);
    const Rect pane = audio_graph_pane(win.split_x, win.sidebar_w, win.win_h, win.dock_h, scenes);
    // The below-session pane (node canvas + header) is ALWAYS live — it's a persistent pane now, not a
    // dock drill-in. The bottom dock's param strip only counts as ours when the dock is actually showing
    // audio params (focus AudioGraph); when the clip editor or a visual node owns the dock, it isn't.
    const bool in_param_dock = (win.focus.kind == vivid::FocusContext::Kind::AudioGraph && my >= win.dock_top());
    return hit(pane, mx, my) || in_param_dock;
}

// Build the audio node's param-curation menu (Gesture A curate / Gesture B reveal). connect_mode omits
// already-wired params and switches dispatch to pin+connect; pending_src is the modulator the wire came
// from. Shared by the audio FSM (AudioNodeGraph::on_down/on_up) and the menu dispatch. `checked` marks a
// shown param (pinned OR wired); a wired param is disabled in curate mode (disconnect to hide).
inline vivid::ui::PopupMenu build_audio_param_menu(vivid::App& app, vivid::ui::AudioNodeGraph& ag, int tr,
                                                   int node_id, float x, float y, bool connect_mode,
                                                   int pending_src) {
    std::vector<vivid::ui::PopupItem> items;
    const int pc = S::session_audio_graph_node_param_count(app.session, tr, node_id);
    for (int p = 0; p < pc; ++p) {
        if (S::session_audio_graph_node_param_hint(app.session, tr, node_id, p) == VIVID_DISPLAY_LFO) continue;
        const bool wired = S::session_audio_graph_node_param_wired(app.session, tr, node_id, p) != 0;
        if (connect_mode && wired) continue;
        vivid::ui::PopupItem it;
        const char* nm = S::session_audio_graph_node_param_name(app.session, tr, node_id, p);
        it.label = nm ? nm : "";
        it.id = p;
        it.checked = wired || ag.is_param_pinned(node_id, p);
        it.enabled = connect_mode ? true : !wired;
        items.push_back(std::move(it));
    }
    const char* tn = "params";   // header = the node's op type (resolved by id → index)
    for (int i = 0, n = S::session_track_audio_graph_node_count(app.session, tr); i < n; ++i)
        if (S::session_track_audio_graph_node_id(app.session, tr, i) == node_id) {
            const char* t = S::session_track_audio_graph_node_type(app.session, tr, i);
            if (t && *t) tn = t;
            break;
        }
    return vivid::ui::popup_param_curate(x, y, node_id, tr, connect_mode, pending_src, tn, std::move(items));
}
// The audio deep view lives in the bottom dock, so a menu opened at the click would extend off the bottom.
// Float it UP so its full height sits above the dock (over the graph), clamped below the top bar.
inline void clamp_audio_menu_onscreen(vivid::Window& win) {
    vivid::ui::PopupMenu& m = win.param_menu;
    const float mh = 22.f + static_cast<float>(m.items.size()) * m.row_h;   // header + rows
    m.y = std::max(30.f, std::min(m.y, win.dock_top() - mh - 8.f));
}
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
    if (!ag_pane_hit(win, app, mx, my)) return false;
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
    using SK = vivid::ui::SpawnKind;   // ADR-0023 step 5: the typed spawn descriptor
    const auto& sp = e.spawn;
    int nid = -1;
    switch (sp.kind) {
        case SK::AudioNativeEffect: nid = S::session_audio_graph_add_op(app.session, tr, sp.type.c_str()); break;
        case SK::AudioNativeSource: nid = S::session_audio_graph_add_source(app.session, tr, sp.type.c_str()); break;
        case SK::AudioNoteOp:       nid = S::session_audio_graph_add_note_op(app.session, tr, sp.type.c_str()); break;
        case SK::AudioModOp:        nid = S::session_audio_graph_add_mod_op(app.session, tr, sp.type.c_str()); break;
        case SK::AudioMidiIn:       nid = S::session_audio_graph_add_midi_in(app.session, tr); break;
        case SK::Note: {   // ADR-0033 P5: a sticky note at the chooser cursor (world coords) — start editing it
            double wx = 0.0, wy = 0.0;
            if (app.audio_graph)
                app.audio_graph->view().to_world(win.audio_chooser.spawn_x(), win.audio_chooser.spawn_y(), wx, wy);
            const int aid = S::session_audio_graph_annotation_add(app.session, tr,
                                static_cast<float>(wx) - 90.f, static_cast<float>(wy) - 48.f);
            if (aid >= 0) {
                win.text_edit_kind = 3; win.text_edit_target = aid; win.text_edit_buf.clear();   // type into it now
                if (app.edit_gateway) app.edit_gateway->note_edit("Add Note", "");
            }
            return;   // a note is not a node — skip the node-selection tail below
        }
        case SK::AudioPluginEffect:
        case SK::AudioPluginSource: {
            const bool src = (sp.kind == SK::AudioPluginSource);
            nid = S::session_audio_graph_add_plugin(app.session, tr, sp.type.c_str(),
                                                    sp.format, src ? 1 : 0, "");   // format carried explicitly
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

// ---- ADR-0050: the add-generator picker (Tab over a scene cell) ----------------------------------
// Places a note generator (Euclid/Chord/RandMelody) into the target cell, with each row drawing a live
// CATALOG preview of the generator's default pattern (via session_op_draw_catalog_thumbnail).

static void generator_chooser_spawn(Window& win, App& app, const vivid::ui::Chooser::Entry& e) {
    if (!app.session || win.gen_pick_track < 0 || win.gen_pick_scene < 0) return;
    if (S::session_place_generator(app.session, win.gen_pick_track, win.gen_pick_scene, e.spawn.type.c_str())) {
        if (app.edit_gateway) app.edit_gateway->note_edit("Add Generator", "");   // ADR-0017/G3 undo capture
    } else {
        std::fprintf(stderr, "[vivid] could not place generator '%s' at track %d scene %d\n",
                     e.label.c_str(), win.gen_pick_track, win.gen_pick_scene);
    }
    win.gen_pick_track = win.gen_pick_scene = -1;
}

bool generator_chooser_open_at(Window& win, App& app, double mx, double my) {
    if (!app.session) return false;
    // Which scene cell is under the cursor? (the grid is shifted right by the sidebar, per clipgrid).
    const int tracks = S::session_track_count(app.session);
    const int scenes = S::session_scene_count(app.session);
    const double dmx = mx - win.sidebar_w;
    int pt = -1, ps = -1;
    for (int t = 0; t < tracks && pt < 0; ++t)
        for (int sc = 0; sc < scenes; ++sc)
            if (vivid::ui::hit(vivid::ui::clip_cell_rect(t, sc), dmx, my)) { pt = t; ps = sc; break; }
    if (pt < 0) return false;

    const int ng = S::session_available_generator_count(app.session);
    if (ng <= 0) return false;
    std::vector<vivid::ui::ChooserEntry> entries;
    for (int i = 0; i < ng; ++i) {
        const char* nm = S::session_available_generator_name(app.session, i);
        if (!nm || !*nm) continue;
        vivid::ui::ChooserEntry e;
        e.label   = nm;
        e.summary = "note generator";
        e.role    = static_cast<VividOperatorRole>(S::session_audio_op_role(app.session, nm));
        e.accent  = vivid::ui::style().control;
        e.spawn   = { vivid::ui::Domain::Audio, vivid::ui::SpawnKind::AudioNoteOp, nm };   // type only; placed via gen picker
        entries.push_back(std::move(e));
    }
    if (entries.empty()) return false;

    win.gen_pick_track = pt; win.gen_pick_scene = ps;
    win.generator_chooser.set_entries(std::move(entries));
    // Per-row CATALOG preview: draw each generator TYPE from its defaults into the row swatch.
    vivid::session::Session* s = app.session;
    win.generator_chooser.set_preview_drawer(
        [s](vivid::ui::Renderer2D& r, const vivid::ui::ChooserEntry& e, float x, float y, float w, float h) -> bool {
            vivid::ui::DrawBridge db{ &r, x, y, x, y, w, h };
            VividThumbnailContext tc{};
            tc.surface_width = w; tc.surface_height = h;
            tc.draw = vivid::ui::make_op_draw_api(&db);
            tc.param_values = nullptr; tc.param_count = 0;   // no placed instance → render from defaults
            const float* a = vivid::ui::style().control;
            tc.accent  = VividColor{ a[0], a[1], a[2], 1.f };
            tc.time    = glfwGetTime() * 2.0;                // gentle ~120bpm animation, transport-independent
            tc.purpose = VIVID_PREVIEW_CATALOG;
            return vivid::session::session_op_draw_catalog_thumbnail(s, e.spawn.type.c_str(), &tc) != 0;
        });
    const Rect cell = vivid::ui::clip_cell_rect(pt, ps);
    win.generator_chooser.show(cell.x + win.sidebar_w, cell.y + cell.h,
                               8.f, vivid::ui::kTopBarH + 8.f,
                               static_cast<float>(win.win_w) - 8.f, static_cast<float>(win.win_h) - 8.f);
    return true;
}

bool generator_chooser_key(Window& win, App& app, int key) {
    if (!win.generator_chooser.open()) return false;
    if (key == GLFW_KEY_ESCAPE) { win.generator_chooser.hide(); return true; }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        if (const auto* e = win.generator_chooser.confirm()) generator_chooser_spawn(win, app, *e);
        return true;
    }
    if (key == GLFW_KEY_DOWN || key == GLFW_KEY_TAB) { win.generator_chooser.move(+1); return true; }
    if (key == GLFW_KEY_UP)        { win.generator_chooser.move(-1); return true; }
    if (key == GLFW_KEY_BACKSPACE) { win.generator_chooser.backspace(); return true; }
    return true;   // swallow everything else while it's up
}

bool generator_chooser_char(Window& win, unsigned int cp) {
    if (!win.generator_chooser.open()) return false;
    win.generator_chooser.type(cp);
    return true;
}

bool generator_chooser_click(Window& win, App& app, double mx, double my) {
    if (!win.generator_chooser.open()) return false;
    bool dismissed = false;
    if (const auto* e = win.generator_chooser.click(mx, my, dismissed)) generator_chooser_spawn(win, app, *e);
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
    // Audio-graph deep view: the editor owns its zoom (ADR-0023 6d).
    if (app.audio_graph) app.audio_graph->on_scroll(app, win, yoff, mx, my);
}

// UI-3 audio node graph deep view (press). Drill in via the Device header "Graph" button; the
// close x returns to the device chain. While drilled in: + FX opens the native-effect picker,
// select a node, drag its param knobs (by node id), remove an effect x, start a rewire from an
// output port, click an edge to disconnect, double-click empty to reset the view else pan. All
// dock clicks are consumed here (returns true) so they never reach the device-chip handlers.
// Right-click a visuals op node -> open its context menu (Open source / Clone & Edit).
bool graph_node_rclick(Window& win, App& app, int button, int action, double mx, double my) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS && app.graph && mx >= win.split_x) {
        const int on = app.graph->op_at(mx, my);
        if (on >= 0) {
            // ADR-0020: resolve ONE contextual edit action for this node (ADR-0027: into a PopupMenu).
            const std::string op_type = app.graph->op_type_at(on);
            vivid::ui::NodeAction act = vivid::ui::NodeAction::None;
            std::string target;
            std::string shader_path, shader_tier;
            for (const auto& e : app.shader_library.entries())
                if (e.name == op_type) { shader_path = e.path; shader_tier = e.tier; break; }
            std::string watched, asset;
            if (shader_tier == "bundled") {          // shipped shader is read-only → fork a copy to edit
                act = vivid::ui::NodeAction::ForkEdit; target = op_type;
            } else if (!shader_tier.empty()) {       // user/project shader → open its .wgsl (watcher reloads)
                act = vivid::ui::NodeAction::OpenSource; target = shader_path;
            } else if (watched = app.hot_reload.source_for(op_type), !watched.empty()) {
                act = vivid::ui::NodeAction::OpenSource; target = watched;   // cloned/user C++ op source
            } else if (auto psrc = app.project_operator_sources.find(op_type);
                       psrc != app.project_operator_sources.end()) {
                // ADR-0054 source-forward: a project-local operator's .cpp ships with the project.
                act = vivid::ui::NodeAction::OpenSource; target = psrc->second;
            } else if (asset = app.graph->op_source_path(on), !asset.empty()) {
                act = vivid::ui::NodeAction::OpenSource; target = asset;      // CustomShader .glsl asset
            } else if (vivid::operator_has_clone_template(app.graph->op_kind_name(on))) {
                act = vivid::ui::NodeAction::CloneEdit;                       // built-in → clone to edit
            }
            const char* label =
                act == vivid::ui::NodeAction::OpenSource ? "Open source in editor"
              : act == vivid::ui::NodeAction::ForkEdit   ? "Fork & edit"
              : act == vivid::ui::NodeAction::CloneEdit  ? "Clone & Edit"
                                                         : "built-in \xC2\xB7 no editable source";
            win.node_menu = vivid::ui::popup_visual_node(static_cast<float>(mx), static_cast<float>(my),
                                                         on, act, label, app.graph->op_kind_name(on), target);
            win.menu.open = false;
            return true;
        }
    }
    return false;
}

// Right-click an AUDIO-GRAPH node → open the "→ visuals" menu carrying (track, node id). Mirrors the
// audio editor's own on_down setup (resolve the selected track, prime the camera/bounds) so the
// cursor→world hit-test lands on the same boxes the editor draws.
bool audio_node_rclick(Window& win, App& app, int button, int action, double mx, double my) {
    if (button != GLFW_MOUSE_BUTTON_RIGHT || action != GLFW_PRESS || !app.audio_graph || !app.session) return false;
    if (!ag_pane_hit(win, app, mx, my)) return false;
    AudioNodeGraph* ag = app.audio_graph;
    const int tr = std::min(std::max(win.sel_track, 0), S::session_track_count(app.session) - 1);
    ag->prime(app, win);   // node-canvas bounds (below-session pane) + param bounds (dock) + selection
    double wmx, wmy; ag->view().to_world(mx, my, wmx, wmy);
    for (const AudioNodeBox& b : ag->layout()) {
        if (wmx >= b.x && wmx < b.x + b.w && wmy >= b.y && wmy < b.y + b.h) {
            // resolve the node's op type (header/label) + whether it's a modulator (control-only) at open
            const char* nm = "node"; bool ismod = false;
            const int nn = S::session_track_audio_graph_node_count(app.session, tr);
            for (int i = 0; i < nn; ++i)
                if (S::session_track_audio_graph_node_id(app.session, tr, i) == b.node_id) {
                    const char* t = S::session_track_audio_graph_node_type(app.session, tr, i);
                    if (t && *t) nm = t;
                    ismod = S::session_track_audio_graph_node_kind(app.session, tr, i) == 5;
                    break;
                }
            win.audio_node_menu = vivid::ui::popup_audio_node(static_cast<float>(mx), static_cast<float>(my),
                                                              tr, b.node_id, ismod, nm);
            win.menu.close(); win.node_menu.close();   // one menu at a time
            return true;
        }
    }
    return false;
}

// Complete an audio-graph rewire: a release over another node's input port connects the edge.
// Returns true when a rewire drag was in progress (consumes the release).
// Node context menu press: "Open source" (custom nodes) or "Clone & Edit" (built-ins). Returns
// true when the menu was open (it always closes + consumes the click).
bool graph_nodemenu(Window& win, App& app, double mx, double my) {
    if (!win.node_menu.open) return false;
    const int nn = win.node_menu.a;   // the visual node id (ADR-0027 payload)
    if (app.graph && win.node_menu.hit_row(static_cast<float>(mx), static_cast<float>(my)) >= 0) {
        switch (static_cast<vivid::ui::NodeAction>(win.node_menu.items[0].id)) {
            case vivid::ui::NodeAction::OpenSource:
                if (!win.node_menu.data.empty()) vivid::platform::open_in_editor(win.node_menu.data);
                break;
            case vivid::ui::NodeAction::ForkEdit: {
                // ADR-0020: fork a shipped (read-only) shader into the user tier, swap the node to
                // the copy, and open it — the always-on shader watcher live-updates saved edits.
                const std::string& base = win.node_menu.data;
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
            case vivid::ui::NodeAction::CloneEdit: {
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
            case vivid::ui::NodeAction::None: break;
        }
    }
    win.node_menu.close();
    return true;
}

// Build the param-curation item list for a VISUAL node: every param, checked when currently shown
// (pinned OR wired), and a wired param disabled (it's shown because of its connection — disconnect to hide).
static std::vector<vivid::ui::PopupItem> visual_param_items(App& app, int node_i, bool connect_mode) {
    std::vector<vivid::ui::PopupItem> items;
    const int pc = app.graph->op_param_count_at(node_i);
    for (int l = 0; l < pc; ++l) {
        const bool wired = app.graph->op_param_wired_at(node_i, l);
        if (connect_mode && wired) continue;   // the reveal menu only offers params you can still connect
        vivid::ui::PopupItem it;
        it.label   = app.graph->op_param_label_at(node_i, l);
        it.id      = l;
        it.checked = wired || app.graph->is_param_pinned(node_i, l);
        it.enabled = connect_mode ? true : !wired;
        items.push_back(std::move(it));
    }
    return items;
}

// Gesture A: left-click a visuals node's header chevron → open its show/hide-params menu.
bool graph_param_curate_click(Window& win, App& app, int button, int action, double mx, double my) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS || !app.graph || mx < win.split_x) return false;
    const int i = app.graph->param_curate_hit(mx, my);
    if (i < 0) return false;
    win.param_menu = vivid::ui::popup_param_curate(static_cast<float>(mx), static_cast<float>(my), i,
                                                   /*audio_track*/ -1, /*connect_mode*/ false, /*pending_src*/ -1,
                                                   app.graph->op_kind_name(i), visual_param_items(app, i, false));
    win.menu.close(); win.node_menu.close();
    return true;
}

// Gesture B: a param wire was dropped on a node body (no visible port) → open the reveal+connect menu.
bool graph_param_reveal_open(Window& win, App& app) {
    if (!app.graph) return false;
    int node_i, src; double sx, sy;
    if (!app.graph->take_param_menu_request(node_i, src, sx, sy)) return false;
    auto items = visual_param_items(app, node_i, /*connect_mode*/ true);
    if (items.empty()) return false;   // nothing left to connect (all params already wired)
    win.param_menu = vivid::ui::popup_param_curate(static_cast<float>(sx), static_cast<float>(sy), node_i,
                                                   /*audio_track*/ -1, /*connect_mode*/ true, /*pending_src*/ src,
                                                   app.graph->op_kind_name(node_i), std::move(items));
    win.menu.close(); win.node_menu.close();
    return true;
}

// Dispatch a click in the param-curation menu: toggle a pin (curate) or pin+connect the parked wire (reveal).
bool graph_parammenu(Window& win, App& app, double mx, double my) {
    if (!win.param_menu.open) return false;
    vivid::ui::PopupMenu& m = win.param_menu;
    const int row = m.hit_row(static_cast<float>(mx), static_cast<float>(my));
    if (app.graph && row >= 0 && m.items[row].enabled) {
        const int node_i = m.a;
        const int param  = m.items[row].id;
        const bool audio = m.data.rfind("audio:", 0) == 0;
        if (!audio) {
            if (m.kind == vivid::ui::PopupMenu::Kind::NodeParamConnect) {
                app.graph->pin_param(node_i, param);
                app.graph->connect_data_to_param(m.b, node_i, param);
            } else {
                app.graph->toggle_param_pin(node_i, param);
            }
        } else if (app.session) {
            const int tr = std::atoi(m.data.c_str() + 6);   // "audio:<tr>"
            if (m.kind == vivid::ui::PopupMenu::Kind::NodeParamConnect) {
                S::session_audio_graph_node_param_pin(app.session, tr, node_i, param);
                S::session_audio_graph_connect_control(app.session, tr, m.b, node_i, param,
                                                       /*amount*/ 1.f, /*curve*/ 0.f, /*invert*/ 0, /*bipolar*/ 0);
                if (app.edit_gateway) app.edit_gateway->note_edit("Connect Modulation", "");
            } else {   // toggle: item.checked (and enabled ⇒ not wired) means currently pinned
                if (m.items[row].checked) S::session_audio_graph_node_param_unpin(app.session, tr, node_i, param);
                else                      S::session_audio_graph_node_param_pin(app.session, tr, node_i, param);
                if (app.edit_gateway) app.edit_gateway->note_edit(m.items[row].checked ? "Unpin Param" : "Pin Param", "");
            }
        }
    }
    m.close();
    return true;
}

}  // namespace vivid::input

// ADR-0023 6d: the audio editor's gesture FSM as methods on AudioNodeGraph (the stateful interaction
// owner). Bodies live here in the input module — not the view header — so they keep their session
// C-API / chooser / plugin-window dependencies local. Domain edits route through the session C-API +
// the EditGateway exactly as the free functions they replaced did.
namespace vivid::ui {
namespace S = vivid::session;

// Open the selected audio-graph node's native plugin editor into a free window slot — VST3 (IPlugView)
// or CLAP (clap.gui), chosen by the node's binding family. No-op for native / sampler / output nodes.
static void open_audio_node_editor(vivid::App& app, vivid::Window& win, int tr, int node_id) {
    if (!app.session || node_id < 0) return;
    const char* name = S::session_track_name(app.session, tr);
    switch (S::session_track_audio_graph_node_plugin_kind(app.session, tr, node_id)) {
        case 1:   // VST3
            if (auto* ctrl = static_cast<Steinberg::Vst::IEditController*>(
                    S::session_audio_graph_node_controller(app.session, tr, node_id)))
                for (int k = 0; k < S::kMaxTracks; ++k) if (!win.fx_win[k]) { win.fx_win[k] = vst3_plugin_window_open(ctrl, name); break; }
            break;
        case 2:   // CLAP
            if (auto* ch = static_cast<vivid::session::ClapHandle*>(
                    S::session_audio_graph_node_clap(app.session, tr, node_id)))
                for (int k = 0; k < S::kMaxTracks; ++k) if (!win.clap_win[k]) { win.clap_win[k] = clap_plugin_window_open(ch, name); break; }
            break;
        default: break;
    }
}

// Press in the audio-graph deep view: the whole dock interaction — the header "Editor" button, source
// key-range handles, the plugin pinned-inspector rows, the native knob strip, param-port drag/click,
// node select / remove / double-click-editor + reposition-drag start, edge disconnect, and empty-space
// double-click-reset / pan-start. Returns true when it consumed the press (always, over the dock). The
// caller guarantees a left-button press.
// Prime the instance from the shell for a draw or a gesture: resolve the selected track, set the
// node-canvas bounds (the below-session pane) AND the param bounds (the dock), and the selection.
// One source of truth so every draw/hit-test path agrees on geometry (ADR-0023: below-session pane).
void AudioNodeGraph::prime(App& app, const Window& win) {
    if (!app.session) return;
    const int tr = std::min(std::max(win.sel_track, 0), S::session_track_count(app.session) - 1);
    set_source(app.session, tr);
    const int scenes = S::session_scene_count(app.session);
    const Rect canv = audio_pane_canvas_rect(audio_graph_pane(win.split_x, win.sidebar_w, win.win_h, win.dock_h, scenes));
    set_bounds(canv.x, canv.y, canv.x + canv.w, canv.y + canv.h);
    const Rect dp = audio_graph_panel(win.win_w, win.win_h, win.dock_h);
    set_param_bounds(dp.x, dp.y, dp.x + dp.w, dp.y + dp.h);
    set_selection(win.sel_audio_node);
    sel_multi_ = &win.audio_sel;   // ADR-0033 P1: point the const draw path at the window-owned set
    edit_anno_ = (win.text_edit_kind == 3) ? win.text_edit_target : -1;   // ADR-0033 P5: live sticky-note edit
    edit_buf_  = &win.text_edit_buf;
}

bool AudioNodeGraph::on_down(App& app, Window& win, double mx, double my, int mods) {
    if (!(app.session && ag_pane_hit(win, app, mx, my)))
        return false;
    const bool m_shift = (mods & GLFW_MOD_SHIFT) != 0;
    const bool m_super = (mods & GLFW_MOD_SUPER) != 0;
    const int tr = std::min(std::max(win.sel_track, 0), S::session_track_count(app.session) - 1);
    prime(app, win);
    const Rect ag_pane = audio_graph_pane(win.split_x, win.sidebar_w, win.win_h, win.dock_h, S::session_scene_count(app.session));
    // (Audio-pane "Re-layout" is now a native View menu item — ⌘L relays out the current audio graph.)
    // "Editor" button (audio-pane header) → open the selected node's native plugin editor (VST3 or CLAP).
    if (win.sel_audio_node >= 0
        && hit(audio_pane_editor_rect(ag_pane), mx, my)) {
        open_audio_node_editor(app, win, tr, win.sel_audio_node);
        return true;
    }
    // ADR-0049: a selected Sampler node's dock IS the SamplerEditor, which processes its own clicks
    // (immediate-mode, in draw). The old dock param-strip handlers below must then be SKIPPED — otherwise
    // they double-process the same click against the retired knob-strip layout and fight the editor (the
    // symptom: a gate/one-shot toggle that sets, then the stale handler starts a knob-drag that reverts it).
    bool sampler_dock = false;
    if (win.sel_audio_node >= 0) {
        const int nc = S::session_track_audio_graph_node_count(app.session, tr);
        for (int i = 0; i < nc; ++i)
            if (S::session_track_audio_graph_node_id(app.session, tr, i) == win.sel_audio_node) {
                const char* ty = S::session_track_audio_graph_node_type(app.session, tr, i);
                sampler_dock = ty && std::strcmp(ty, "Sampler") == 0;
                break;
            }
    }
    // The SamplerEditor owns the whole dock (immediate-mode input in draw). CONSUME any press/drag inside
    // it so it can't fall through to the graph-canvas pan below — dragging the waveform must not pan the
    // graph. (The dock-resize strip at the very top stays draggable.)
    if (sampler_dock && my >= win.dock_top() && !hit(win.dock_resize_rect(), mx, my)) return true;
    if (win.sel_audio_node >= 0 && !sampler_dock && sel_is_source(win.sel_audio_node)) {   // key-range drag handles (source node)
        int lo = 0, hi = 127;
        S::session_audio_graph_node_key_range_get(app.session, tr, win.sel_audio_node, &lo, &hi);
        if (hit(key_lo_rect(win.sel_audio_node), mx, my)) { key_drag = 0; key_v0 = lo; key_y0 = my; return true; }
        if (hit(key_hi_rect(win.sel_audio_node), mx, my)) { key_drag = 1; key_v0 = hi; key_y0 = my; return true; }
    }
    // Curated inspector (Phase 2b): a plugin node's pinned params are vertical rows, not a knob grid.
    if (win.sel_audio_node >= 0 && !sampler_dock && is_plugin_node(win.sel_audio_node)) {
        if (hit(add_param_button_rect(win.sel_audio_node), mx, my)) {   // "+ Add param" palette
            vivid::input::param_chooser_open(win, app, win.sel_audio_node, mx, my);
            return true;
        }
        for (const auto& row : pinned_rows(win.sel_audio_node)) {
            if (hit(Rect{ row.mapdot.x - 4.f, row.mapdot.y - 2.f, row.mapdot.w + 8.f, row.mapdot.h + 8.f }, mx, my)) {
                const float menu_w = 168.f, item_h = 24.f, marg = 8.f, menu_h = kNumMapSources * item_h;
                const float fx = std::min(static_cast<float>(mx), win.win_w - menu_w - marg);
                const float fy = std::max(marg + 22.f, std::min(static_cast<float>(my), win.dock_top() - menu_h - marg));
                win.map_menu = vivid::ui::popup_map_sources(fx, fy, win.sel_audio_node);
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
                const NodeWidget wk = static_cast<NodeWidget>(row.widget);
                if (wk == NodeWidget::Toggle) {   // click flips
                    const float mid = mn + (mxx - mn) * 0.5f;
                    S::session_audio_graph_node_param_set(app.session, tr, win.sel_audio_node, row.index, v > mid ? mn : mxx);
                    if (app.edit_gateway) app.edit_gateway->note_edit("Toggle Param", "");   // ADR-0017/G3
                    return true;
                }
                if (wk == NodeWidget::Enum) {   // open the choice list (a real dropdown)
                    vivid::input::param_enum_chooser_open(win, app, win.sel_audio_node, row.index, mx, my);
                    return true;
                }
                // slider: start a HORIZONTAL drag and apply immediately at the click x
                param_drag = row.index; param_horiz = true;
                param_rx = row.widget_rect.x; param_rw = row.widget_rect.w;
                const float norm = std::clamp(static_cast<float>(mx - row.widget_rect.x) / row.widget_rect.w, 0.f, 1.f);
                S::session_audio_graph_node_param_set(app.session, tr, win.sel_audio_node, row.index, mn + norm * (mxx - mn));
                return true;
            }
        }
        // a click in the band that hit no row falls through to node select (the band is below the nodes)
    }
    if (win.sel_audio_node >= 0 && !sampler_dock && !is_plugin_node(win.sel_audio_node)) {   // native knob strip (by node id)
        for (const auto& c : param_cells(win.sel_audio_node)) {
            // The map dot (top-right of the cell) takes priority over the knob rect it sits inside.
            const Rect dd = ag_param_map_dot(c);
            if (hit(Rect{ dd.x - 4.f, dd.y - 2.f, dd.w + 8.f, dd.h + 9.f }, mx, my)) {
                const float menu_w = 168.f, item_h = 24.f, marg = 8.f, menu_h = kNumMapSources * item_h;
                const float fx = std::min(static_cast<float>(mx), win.win_w - menu_w - marg);
                const float fy = std::max(marg + 22.f, std::min(static_cast<float>(my), win.dock_top() - menu_h - marg));
                win.map_menu = vivid::ui::popup_map_sources(fx, fy, win.sel_audio_node);
                win.map_param = c.index;
                return true;
            }
            if (mx >= c.x && mx < c.x + c.w && my >= c.y && my < c.y + c.h) {
                const float mn = S::session_audio_graph_node_param_min(app.session, tr, win.sel_audio_node, c.index);
                const float mxx = S::session_audio_graph_node_param_max(app.session, tr, win.sel_audio_node, c.index);
                const float v = S::session_audio_graph_node_param_get(app.session, tr, win.sel_audio_node, c.index);
                param_drag = c.index;
                param_v0 = (mxx > mn) ? std::clamp((v - mn) / (mxx - mn), 0.f, 1.f) : 0.f;
                param_y0 = my; return true;
            }
        }
        // UI-4a: clicking the LFO waveform preview cycles the enum (wraps min..max).
        for (const auto& cp : compound_previews()) {
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
    // ADR-0023 #3: the graph draws WORLD-space, so graph-area hit-tests (cards/ports below) compare the
    // cursor in WORLD coords. The chrome above (dock button, key handles + param strip = the param band,
    // popover placement) stays screen-space and keeps using mx,my.
    double wmx, wmy; canvas_.view().to_world(mx, my, wmx, wmy);
    const auto boxes = layout();
    // Gesture A: a header chevron opens the show/hide-params menu (before body-select would grab the node).
    if (const int chev = param_curate_hit(wmx, wmy); chev >= 0) {
        win.param_menu = build_audio_param_menu(app, *this, tr, chev, static_cast<float>(mx),
                                                static_cast<float>(my), /*connect_mode*/ false, /*pending_src*/ -1);
        clamp_audio_menu_onscreen(win);   // the dock is at the bottom → float the menu up over the graph
        win.menu.close(); win.node_menu.close();
        win.sel_audio_node = chev;
        return true;
    }
    for (const auto& b : boxes) {   // start a rewire drag from an output port (release connects)
        // No user-drawable output on ENGINE-MANAGED nodes: the Output sink (2) and the note-source
        // infrastructure — MIDI In (3), Clip (6), Selector (7), Gen (8). reconcile_note_subgraph owns
        // and recomputes their note fan-out every publish, so a hand-drawn edge from them wouldn't
        // survive. Only instruments/FX (0/1), note effects (4), and modulators (5) start a wire.
        if (b.kind == 2 || b.kind == 3 || b.kind == 6 || b.kind == 7 || b.kind == 8) continue;
        if (hit(out_port_rect(b), wmx, wmy)) { wire_from = b.node_id; return true; }
    }
    // ADR-0022: the "+ param" row on a plugin card opens the searchable picker to expose one more param.
    for (const auto& b : boxes)
        if (hit(add_param_port_rect(b), wmx, wmy)) {
            win.sel_audio_node = b.node_id;
            vivid::input::param_chooser_open(win, app, b.node_id, mx, my);
            return true;
        }
    // ADR-0022: clicking a WIRED (magenta) param port opens the modulation shape editor for its edge.
    for (const auto& b : boxes) {
        const int slot = param_port_hit(b, wmx, wmy);
        if (slot < 0) continue;
        const std::vector<int> exp = exposed_params(b.node_id);
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
        const float px = std::min(static_cast<float>(mx), win.win_w - kModEdW - 8.f);
        win.mod_editor = { true, px, static_cast<float>(my), b.node_id, param, from };
        win.sel_audio_node = b.node_id;
        return true;
    }
    // ADR-0033 P5: sticky notes are top-most (drawn last), so hit them before nodes. Delete × removes;
    // double-click on the body edits its text; a single click starts a drag. Positions are WORLD coords.
    {
        const int na = S::session_audio_graph_annotation_count(app.session, tr);
        for (int i = na - 1; i >= 0; --i) {
            int aid = 0; float ax = 0.f, ay = 0.f, aw = 0.f, ah = 0.f;
            if (!S::session_audio_graph_annotation_at(app.session, tr, i, &aid, &ax, &ay, &aw, &ah)) continue;
            if (hit({ ax + aw - 15.f, ay + 2.f, 14.f, 14.f }, wmx, wmy)) {   // delete ×
                S::session_audio_graph_annotation_remove(app.session, tr, aid);
                if (app.edit_gateway) app.edit_gateway->note_edit("Delete Note", "");
                return true;
            }
            if (wmx >= ax && wmx < ax + aw && wmy >= ay && wmy < ay + ah) {   // body: double-click edits, else drag
                const double now = glfwGetTime();
                if (last_anno_ == aid && now - last_anno_t_ < 0.35) {         // double-click → edit text
                    win.text_edit_kind = 3; win.text_edit_target = aid;
                    const char* t = S::session_audio_graph_annotation_text(app.session, tr, aid);
                    win.text_edit_buf = t ? t : "";
                    last_anno_t_ = -1;
                } else {                                                     // single click → start a drag
                    last_anno_ = aid; last_anno_t_ = now;
                    anno_drag_ = aid; anno_dx_ = static_cast<float>(wmx) - ax; anno_dy_ = static_cast<float>(wmy) - ay;
                }
                return true;
            }
        }
    }
    for (const auto& b : boxes) {   // remove-x (effects) or select — both by node id
        if (b.kind == 1 && hit(remove_rect(b), wmx, wmy)) {
            S::session_audio_graph_remove_node(app.session, tr, b.node_id);
            if (win.sel_audio_node == b.node_id) win.sel_audio_node = vivid::Window::kNoAudioNode;
            if (app.edit_gateway) app.edit_gateway->note_edit("Remove Audio Node", "");   // ADR-0017/G3
            return true;
        }
        if (wmx >= b.x && wmx < b.x + b.w && wmy >= b.y && wmy < b.y + b.h) {
            // ADR-0033 P1: ⇧/⌘-click toggles this card's membership (no drag). The Output sink (kind 2)
            // has no params and never joins the selection.
            if ((m_shift || m_super) && b.kind != 2) {
                win.audio_sel.toggle(b.node_id);
                win.sel_audio_node = win.audio_sel.empty() ? vivid::Window::kNoAudioNode : win.audio_sel.primary();
                return true;
            }
            win.sel_audio_node = (b.kind == 2) ? vivid::Window::kNoAudioNode : b.node_id;   // output has no params
            // Plain click on an unselected card replaces the set; clicking one already selected keeps the
            // set (whole group drags together) but re-anchors the primary. Output clears the selection.
            if (b.kind == 2) win.audio_sel.clear();
            else if (!win.audio_sel.contains(b.node_id)) win.audio_sel.replace(b.node_id);
            else win.audio_sel.set_primary(b.node_id);
            // Double-click a plugin node → open its native editor (VST3 or CLAP).
            const double now = glfwGetTime();
            if (last_node == b.node_id && now - last_node_t < 0.35) {
                open_audio_node_editor(app, win, tr, b.node_id);
                last_node_t = -1;
            } else { last_node = b.node_id; last_node_t = now; }
            node_drag = b.node_id;                                   // start a reposition drag (any node)
            node_dx = wmx - b.x;                                     // grab offset in world units (cursor already world)
            node_dy = wmy - b.y;
            // Snapshot every selected node's world position so on_move shifts them by one shared delta.
            grp_start_.clear();
            for (const auto& gb : boxes)
                if (win.audio_sel.contains(gb.node_id)) grp_start_.push_back({ gb.node_id, { gb.x, gb.y } });
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
    // ADR-0033 P1: ⇧-drag on empty space rubber-bands a marquee (⌘ makes it additive) instead of panning.
    if (m_shift) {
        marquee_ = true; marq_add_ = m_super;
        marq_x0_ = marq_x1_ = wmx; marq_y0_ = marq_y1_ = wmy;   // world corners
        return true;
    }
    // Empty space: double-click resets the view (2i); otherwise start a pan drag.
    const double now = glfwGetTime();
    if (now - last_click_t < 0.30) {   // double-click resets the camera to the fitted view (panel origin)
        canvas_.reset(graph_region()); view_init_ = true;   // ADR-0023 #3d: shared camera reset
        last_click_t = -1; return true;
    }
    last_click_t = now;
    panning = true; pan_last_mx = mx; pan_last_my = my;   // ADR-0023 #3d: incremental pan (shared canvas_.pan)
    return true;   // consume other clicks in the graph
}

// Release: end any in-flight drag, then (if a rewire was in progress) connect the edge over the port
// under the cursor. Returns true when a rewire was completed (the caller closes the undo group).
bool AudioNodeGraph::on_up(App& app, Window& win, double mx, double my) {
    param_drag = -1; param_horiz = false; node_drag = -1; key_drag = -1; panning = false; anno_drag_ = -1;
    grp_start_.clear();
    if (marquee_) {   // ADR-0033 P1: resolve the marquee against every laid-out card
        marquee_ = false;
        if (app.session) {
            std::vector<SelItem> items;
            for (const auto& b : layout()) items.push_back({ b.node_id, { b.x, b.y, b.w, b.h } });
            win.audio_sel.resolve_marquee(items,
                { float(marq_x0_), float(marq_y0_), float(marq_x1_ - marq_x0_), float(marq_y1_ - marq_y0_) },
                marq_add_);
            win.sel_audio_node = win.audio_sel.empty() ? vivid::Window::kNoAudioNode : win.audio_sel.primary();
        }
        return true;   // consume; the global undo group closes cleanly (no edit was noted)
    }
    if (wire_from < 0 || !app.session) return false;
    const int tr = std::min(std::max(win.sel_track, 0), S::session_track_count(app.session) - 1);
    prime(app, win);   // node-canvas bounds (below-session pane) so the rewire ports resolve there
    // ADR-0015: what SIGNAL the dragged wire carries follows from what the source node emits. A MidiIn
    // (kind 3) / note effect (kind 4) emit notes (NOTE edge); a modulator (kind 5) emits CONTROL to a
    // PARAM port; everything else is audio. (Unambiguous today because no node emits both.)
    // ADR-0023 #3: the drop target is a port on a WORLD-space card — hit-test the cursor in world coords.
    double wmx, wmy; canvas_.view().to_world(mx, my, wmx, wmy);
    int from_kind = -1;
    for (const auto& b : layout()) if (b.node_id == wire_from) { from_kind = b.kind; break; }
    const bool note_wire = (from_kind == 3 || from_kind == 4);
    if (from_kind == 5) {   // modulator -> a param PORT (control edge to that exact param, ADR-0022)
        bool connected = false;
        for (const auto& b : layout()) {
            if (b.node_id == wire_from) continue;
            const int slot = param_port_hit(b, wmx, wmy);
            if (slot < 0) continue;
            const std::vector<int> exp = exposed_params(b.node_id);
            if (slot >= static_cast<int>(exp.size())) continue;
            // Default shape: full-range unipolar sweep (amount 1, curve 0); a shape editor is next.
            if (S::session_audio_graph_connect_control(app.session, tr, wire_from, b.node_id,
                                                       exp[slot], /*amount*/1.f, /*curve*/0.f,
                                                       /*invert*/0, /*bipolar*/0)
                && app.edit_gateway)
                app.edit_gateway->note_edit("Connect Modulation", "");
            connected = true;
            break;
        }
        if (!connected) {   // Gesture B: dropped on a node body (no visible port) → reveal+connect menu
            for (const auto& b : layout()) {
                if (b.node_id == wire_from || b.kind == 2) continue;
                if (wmx >= b.x && wmx < b.x + b.w && wmy >= b.y && wmy < b.y + b.h) {
                    win.param_menu = build_audio_param_menu(app, *this, tr, b.node_id, static_cast<float>(mx),
                                                            static_cast<float>(my), /*connect_mode*/ true,
                                                            /*pending_src*/ wire_from);
                    clamp_audio_menu_onscreen(win);
                    win.menu.close(); win.node_menu.close();
                    break;
                }
            }
        }
        wire_from = -1;
        return true;
    }
    for (const auto& b : layout()) {
        if (b.node_id == wire_from || !hit(in_port_rect(b), wmx, wmy)) continue;
        // A note wire may land on an instrument (kind 0) or another note effect; an audio wire may not
        // land on a source.
        if (note_wire) {
            if (b.kind == 0 || b.kind == 4) {
                S::session_audio_graph_connect_kind(app.session, tr, wire_from, b.node_id, 1);
                if (app.edit_gateway) app.edit_gateway->note_edit("Connect Audio", "");   // ADR-0017/G3
            }
        } else if (b.kind != 0 && b.kind != 3 && b.kind != 4) {
            S::session_audio_graph_connect_kind(app.session, tr, wire_from, b.node_id, 0);
            if (app.edit_gateway) app.edit_gateway->note_edit("Connect Audio", "");
        }
        break;
    }
    wire_from = -1;
    return true;
}

// Scroll-wheel zoom of the audio-graph deep view, around the cursor (keeps the world point under the
// cursor fixed). The zoom clamp ([0.35,4.0]) is the audio graph's own policy.
void AudioNodeGraph::on_scroll(App& app, Window& win, double yoff, double mx, double my) {
    if (!app.session) return;
    prime(app, win);   // node-canvas bounds (below-session pane) for the zoom hit-region
    const Rect gr = graph_region();
    if (mx >= gr.x && mx < gr.x + gr.w && my >= gr.y && my < gr.y + gr.h) {
        canvas_.zoom_at(mx, my, std::pow(1.12f, static_cast<float>(yoff)));   // ADR-0023 #3d: shared zoom-around-cursor
        view_init_ = true;
    }
}

}  // namespace vivid::ui
