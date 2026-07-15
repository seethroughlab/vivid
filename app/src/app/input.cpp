#include "app/input.h"
#include "app/input_internal.h"   // Phase D (#8): per-concern input controllers

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window.h"
#include "ui/layout.h"
#include "ui/session_view.h"      // meter_hit
#include "ui/mapping_overview.h"  // ov_geom, ov_row
#include "ui/node_graph.h"
#include "audio/vst3_host.h"
#include "app/frame.h"   // open_popout / close_popout
#include "transport.h"   // Transport play/stop (toggle_playing)
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
    // The audio graph's Tab chooser, while open, owns the keyboard — before musical typing and the
    // global shortcuts, or typing an op name would play notes and toggle overlays.
    if (win->audio_chooser.open()) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT) vivid::input::audio_chooser_key(*win, *app, key);
        return;
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
    // Tab -> open the chooser at the cursor. It is the ONE way to add a node, in BOTH graphs:
    //   over the visuals column  -> the visuals operator chooser (ADR-0014)
    //   over the audio graph     -> the unified audio catalog (native ops + VST3 + CLAP)
    if (key == GLFW_KEY_TAB) {
        double mx, my; glfwGetCursorPos(w, &mx, &my);
        if (app->graph && mx >= win->split_x && my >= vivid::ui::kTopBarH && my < win->dock_top()) {
            app->graph->chooser_show(mx, my); return;
        }
        if (vivid::input::audio_chooser_open_at(*win, *app, mx, my)) return;
    }

    // File shortcuts (⌘N/⌘O/⌘S/⇧⌘S) are owned by the native File menu (AppKit intercepts
    // them before GLFW), so they're not handled here.
    if (key == GLFW_KEY_V && app->vgraph) {  // toggle the visuals generator (also via the generator node)
        app->vgraph->set_generator(app->vgraph->generator() == "Video" ? "Plasma" : "Video");
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
    if (!win) return;
    if (vivid::input::audio_chooser_char(*win, cp)) return;    // the audio chooser has the keyboard
    if (win->app->graph && win->app->graph->chooser_open()) win->app->graph->chooser_char(cp);
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
    // Zoom whichever node graph is under the cursor (visuals node graph / audio-graph deep view).
    vivid::input::graph_scroll(*win, *win->app, yoff, mx, my);
}

void mouse_button_callback(GLFWwindow* w, int button, int action, int mods) {
    auto* win = static_cast<vivid::Window*>(glfwGetWindowUserPointer(w));
    if (!win) return;
    // Track left-button held state first (before any early return) so the OpEditor (UI-4b), which
    // reads it during draw, sees presses/releases even when another handler consumes the event.
    if (button == GLFW_MOUSE_BUTTON_LEFT) win->mouse_left_down = (action == GLFW_PRESS);
    vivid::App* app = win->app;
    double mx, my; glfwGetCursorPos(w, &mx, &my);
    const double dmx = mx - win->sidebar_w;   // DAW-pane coords (the grid is shifted right by the sidebar)
    const int tracks = app->session ? vivid::session::session_track_count(app->session) : 0;
    const int scenes = app->session ? vivid::session::session_scene_count(app->session) : 0;

    // The audio Tab chooser, while open, is modal: it owns the next click (pick a row / dismiss).
    if (win->audio_chooser.open() && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (vivid::input::audio_chooser_click(*win, *app, mx, my)) return;
    }
    // Top transport bar: play/pause + record + metronome (M6).
    if (vivid::input::transport_mouse(*win, *app, button, action, mx, my)) return;
    // Browser sidebar toggle.
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && hit(vivid::ui::sidebar_toggle_rect(), mx, my)) {
        win->sidebar_w = (win->sidebar_w > 0.f) ? 0.f : vivid::ui::kSidebarW;
        return;
    }
    // ADR-0014: the floating OUTPUT preview. It sits ON TOP of the graph canvas, so its handles are
    // tested BEFORE the graph gets the press (below) — otherwise dragging the preview would pan the
    // canvas underneath it.
    if (win->preview_show && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        // The Output node's params are the truth for where the output is shown (ADR-0014), so these
        // buttons WRITE THE PARAMS; the frame loop reconciles the actual windows from them.
        if (hit(win->preview_close(), mx, my)) {
            if (app->vgraph) app->vgraph->set_output_param("preview", 0.f);
            return;
        }
        if (hit(vivid::ui::preview_popout_rect(win->preview_x, win->preview_y, win->preview_w), mx, my)) {
            if (app->vgraph) app->vgraph->set_output_param("launch", win->popout ? 0.f : 1.f);
            return;
        }
        if (hit(win->preview_grip(), mx, my)) {
            win->preview_resize = true; win->preview_grab_x = mx - win->preview_w; return;
        }
        if (hit(win->preview_header(), mx, my)) {
            win->preview_drag = true;
            win->preview_grab_x = mx - win->preview_x; win->preview_grab_y = my - win->preview_y;
            return;
        }
        if (hit(win->preview_panel(), mx, my)) return;   // clicks on the output itself: consume, don't pan
    }
    // The graph "Re-layout" chrome (top-right of the visuals column).
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && app->graph
        && hit(vivid::ui::graph_relayout_rect(win->win_w, win->win_h, win->split_x, win->dock_h), mx, my)) {
        app->graph->layout_nodes();
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
        win->gain_drag = -1; win->param_drag = -1; win->ag_param_drag = -1; win->ag_param_horiz = false; win->ag_node_drag = -1; win->ag_key_drag = -1; win->ag_panning = false;
        win->preview_drag = false; win->preview_resize = false;
        // Complete an audio-graph rewire: release over another node's input port connects the edge.
        if (vivid::input::graph_rewire_release(*win, *app, mx, my)) return;
        vivid::input::clipgrid_release(*win, *app, mx, my, mods, tracks, scenes);   // clip drop (grid/pool); no-op if no drag
        if (app->graph) app->graph->on_up(mx, my);
        return;
    }
    if (action != GLFW_PRESS) return;

    // Browser sidebar (left column) — the CLIPS pool. (The PLUGINS panel is gone: adding a node is
    // Tab in the graph, one path, over the unified catalog. A browser that could only ever offer
    // plugins — never native ops — was half a catalog behind a second add gesture.)
    if (win->sidebar_w > 0.f && mx < win->sidebar_w && my >= vivid::ui::kTopBarH && my < win->dock_top()) {
        vivid::input::clipgrid_pool_press(*win, *app, mx, my);   // remove (x) / drag a pool clip
        return;   // consume all clicks over the sidebar
    }

    // Characteristics menu: pick a characteristic -> spawn a bridge data node in the graph.
    if (vivid::input::dock_char_menu(*win, *app, mx, my)) return;
    // Node context menu: "Open source" (custom nodes) or "Clone & Edit" (built-ins).
    if (vivid::input::graph_nodemenu(*win, *app, mx, my)) return;
    // Device pickers (priority): FX effect / +Track instrument / mapping source.
    if (vivid::input::dock_menus(*win, *app, mx, my, tracks)) return;
    if (!app->session) return;

    // A meter (master or per-track) -> open its characteristic menu (left-click).
    if (vivid::input::clipgrid_meter_menu(*win, *app, mx, my, tracks, scenes)) return;
    // Track header: × removes the track; otherwise select it. "+ Track" opens the picker.
    if (vivid::input::clipgrid_track_header(*win, *app, mx, my, tracks)) return;

    // Bottom dock interactions. If a visual node is selected, the dock is its
    // inspector: knobs edit the node's base param values (vertical drag). Routed through the
    // explicit focus (UI-1) — the single source of truth shared with the draw path — not a
    // re-derived selected_op. A close (x) in the header exits the focus back to the device view.
    if (win->focus.kind == vivid::FocusContext::Kind::VisualNode && app->graph && my >= win->dock_top()) {
        if (hit(dock_close_rect(win->win_w, win->win_h, win->dock_h), mx, my)) {
            app->graph->select_op(-1); return;   // close the visual-node inspector -> device view
        }
        // UI-4b: "Editor" button → drill into the op's custom editor (only present when it has one).
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && app->graph->op_has_editor(win->focus.node)
            && hit(vivid::ui::dock_op_editor_button_rect(win->win_w, win->win_h, win->dock_h), mx, my)) {
            win->show_op_editor = true; return;
        }
    }
    // UI-4b: the operator editor owns the dock while drilled in. Close × returns to the node
    // inspector; every other press is consumed here (the editor self-edits from the live mouse
    // state during draw, so there is no per-widget hit-test on this side).
    if (win->focus.kind == vivid::FocusContext::Kind::OpEditor && my >= win->dock_top()
        && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (hit(dock_close_rect(win->win_w, win->win_h, win->dock_h), mx, my)) { win->show_op_editor = false; return; }
        // UI-5: "Float" pops the editor out into its own window (opened next tick) + closes the dock
        // editor (returns to the node inspector). The window takes over from here.
        if (hit(vivid::ui::dock_op_float_button_rect(win->win_w, win->win_h, win->dock_h), mx, my)) {
            win->want_float_node = win->focus.node; win->show_op_editor = false; return;
        }
        return;   // consume clicks inside the editor region
    }
    // UI-3 audio node graph deep view: all its dock interaction (select / param / +FX / remove /
    // rewire / edge-disconnect / pan) + the Device header "Graph" drill-in button.
    if (vivid::input::graph_audio_dock(*win, *app, button, action, mx, my)) return;
    if (vivid::input::dock_inspector(*win, *app, mx, my)) return;   // visual-node param inspector (consumes dock)
    // mixer: ARM buttons (record-arm) then gain sliders.
    if (vivid::input::clipgrid_mixer(*win, *app, mx, my, tracks, scenes)) return;
    if (app->graph && app->graph->on_down(mx, my)) return;  // node graph consumed it (it owns the visuals column)
    // clip cells (single-click arms/launches, double-click opens editor) + scene-launch buttons.
    if (vivid::input::clipgrid_cells(*win, *app, mx, my, tracks, scenes)) return;
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
