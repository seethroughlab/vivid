// Vivid PoC — Step 1 foundation: window + WebGPU (Classic GpuContext) +
// miniaudio device + shared transport + test tone.
//
// Proves the shared shell: a window clears each frame (its colour pulses on the
// beat), while the audio thread produces a test tone and advances the master
// transport that the frame thread reads. Half A (VST3/MIDI/session) and Half B
// (shader/analysis) build on this spine.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <cstdio>
#include <cmath>

#include "gpu/gpu_context.h"
#include "gpu/gpu_util.h"
#include "transport.h"
#include "audio/vst3_host.h"
#include "ui/renderer_2d.h"
#include "ui/node_graph.h"
#include "ui/ui_style.h"
#include "ui/layout.h"
#include "app/app_state.h"
#include "audio/audio_callback.h"
#include "ui/mapping_overview.h"
#include "ui/session_view.h"
#include "cli/control_server.h"
#include "ui/clip_editor.h"
#include "persist.h"
#include "gpu/shader_op.h"
#include "audio/vst3_plugin_window.h"
#include "platform/macos_frame_timer.h"
#include "gpu/effect_op.h"
#include "gpu/render_target.h"
#include "gpu/visual_graph.h"
#include "gpu/texture_source.h"
#include "gpu/video_player.h"
#include <dirent.h>
#include <vector>
#include <string>
#include <algorithm>
#include "miniaudio.h"

namespace {

using namespace vivid::ui;  // Rect/hit, grid + dock geometry, constants (ui/layout.h)

// CtxMenu + AudioState moved to app/app_state.h; audio_callback to
// audio/audio_callback.cpp (both shared with the extracted modules).

// Record a single render pass that clears `view` to (r,g,b).
void clear_pass(WGPUCommandEncoder encoder, WGPUTextureView view, float r, float g, float b) {
    WGPURenderPassColorAttachment color{};
    color.view = view;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;  // required for 2D attachments in v29
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = WGPUColor{ r, g, b, 1.0 };

    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

// Rect/hit, the session-grid + dock geometry, and shared constants now live in
// ui/layout.h (brought in unqualified by `using namespace vivid::ui` above).

// Resizable shell: live window size + the draggable DAW|visuals splitter x.
static int   g_win_w = 1280, g_win_h = 800;   // logical (point) size — all UI layout
static int   g_fb_w  = 1280, g_fb_h  = 800;   // framebuffer (physical) size — the surface
static float g_dpi   = 1.0f;                   // g_fb_w / g_win_w (2.0 on retina)
static float g_split_x = 512.f;
static float g_dock_h  = 210.f;   // bottom device-view dock height
// Thin wrappers binding ui/layout.h's window-relative geometry to the live shell
// globals, so existing call sites stay parameterless. (DockGeom, dock_knob, and
// dock_knob_map need no window state and are used directly from ui/layout.h.)
inline float dock_top()        { return vivid::ui::dock_top(g_win_h, g_dock_h); }
inline Rect viewer_rect()      { return vivid::ui::viewer_rect(g_win_w, g_split_x); }
inline Rect splitter_rect()    { return vivid::ui::splitter_rect(g_win_h, g_dock_h, g_split_x); }
inline Rect dock_resize_rect() { return vivid::ui::dock_resize_rect(g_win_w, g_win_h, g_dock_h); }
inline DockGeom dock_geom()    { return vivid::ui::dock_geom(g_win_w, g_win_h, g_dock_h); }
inline Rect dock_chip(int i)   { return vivid::ui::dock_chip(i, g_win_h, g_dock_h); }
inline Rect dock_chip_x(int i) { return vivid::ui::dock_chip_x(i, g_win_h, g_dock_h); }

// Visuals generator source: 0 = plasma shader, 1 = texture (image/video). UI thread.
static int g_visual_source = 0;
static bool g_show_mappings = false;   // P28: the mapping-overview overlay (toggle: M)

static vivid::VisualGraph*      g_vgraph = nullptr;   // source of truth for the generator
// Video source: a folder of clips, cycled with N. (UI/main thread only.)
static VideoPlayer*             g_video = nullptr;
static std::vector<std::string> g_video_paths;
static int                      g_video_idx = -1;
static void load_video_at(int i) {
    if (g_video_paths.empty()) return;
    const int n = static_cast<int>(g_video_paths.size());
    g_video_idx = ((i % n) + n) % n;
    if (g_video) { video_close(g_video); g_video = nullptr; }
    g_video = video_open(g_video_paths[g_video_idx].c_str());
    if (g_video) {
        video_play(g_video, g_visual_source == 1);
        std::fprintf(stderr, "[vivid] video [%d/%d]: %s\n", g_video_idx + 1, n, g_video_paths[g_video_idx].c_str());
    }
}

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

int main() {
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // WebGPU owns the surface
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Vivid PoC — foundation", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "glfwCreateWindow failed\n"); glfwTerminate(); return 1; }

    // Retina/HiDPI: render at the framebuffer (physical) resolution; lay out the UI
    // in logical points. g_dpi bridges them (2.0 on retina) -> crisp text + shapes.
    glfwGetWindowSize(window, &g_win_w, &g_win_h);
    glfwGetFramebufferSize(window, &g_fb_w, &g_fb_h);
    g_dpi = (g_win_w > 0) ? static_cast<float>(g_fb_w) / static_cast<float>(g_win_w) : 1.0f;

    vivid::GpuContext gpu;
    if (!gpu.init(window, static_cast<uint32_t>(g_fb_w), static_cast<uint32_t>(g_fb_h))) {
        std::fprintf(stderr, "GpuContext init failed: %s\n", gpu.last_error().c_str());
        return 1;
    }

    vivid::ui::Renderer2D ui;
    if (!ui.init(gpu.device(), gpu.surface_format(), VIVID_FONT_PATH, 15.0f, g_dpi))
        std::fprintf(stderr, "[vivid] Renderer2D init failed (UI disabled)\n");

    // Composable visuals chain (generator -> feedback -> blur -> viewer).
    const uint32_t kRtW = static_cast<uint32_t>(kViewW), kRtH = static_cast<uint32_t>(kViewH);
    vivid::VisualGraph vgraph;
    if (!vgraph.init(gpu.device(), gpu.queue(), gpu.surface_format(), kRtW, kRtH))
        std::fprintf(stderr, "[vivid] visual graph init failed (viewer disabled)\n");
    g_vgraph = &vgraph;

    // Texture source (image/video) — seeded with a test pattern; P19b feeds video.
    vivid::TextureSource srcTex;
    srcTex.init(gpu.device(), 512, 288, gpu.surface_format());
    { auto pat = vivid::gen_test_pattern(512, 288); srcTex.upload(gpu.queue(), pat.data()); }

    // Scan the video folder and open the first clip (N cycles, V shows it).
    {
        const char* dir = "/Users/jeff/Movies/Gero Individual Reel Files";
        if (DIR* d = opendir(dir)) {
            while (dirent* e = readdir(d)) {
                std::string n = e->d_name;
                if (n.size() > 4 && n.compare(n.size() - 4, 4, ".mp4") == 0)
                    g_video_paths.push_back(std::string(dir) + "/" + n);
            }
            closedir(d);
            std::sort(g_video_paths.begin(), g_video_paths.end());
        }
        if (!g_video_paths.empty()) load_video_at(0);
        std::fprintf(stderr, "[vivid] %zu video clips found\n", g_video_paths.size());
    }

    vivid::ui::NodeGraph graph;
    graph.set_visual_graph(&vgraph);   // show the op-chain; generator node toggles Plasma/Video
    vivid::ui::ClipEditor clip_editor;

    Transport transport;
    AudioState audio_state{};
    audio_state.transport = &transport;  // player set after the device opens
    audio_state.graph = &graph;
    audio_state.editor = &clip_editor;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 0;  // device default
    cfg.dataCallback = audio_callback;
    cfg.pUserData = &audio_state;

    ma_device device;
    bool audio_ok = (ma_device_init(nullptr, &cfg, &device) == MA_SUCCESS);
    if (audio_ok) {
        // Now that we know the device sample rate, scan + load an instrument.
        audio_state.session = vivid_poc::session_create(device.sampleRate);
        std::fprintf(stderr, "[vivid] session: %d tracks (track 0: %s)\n",
                     audio_state.session ? vivid_poc::session_track_count(audio_state.session) : 0,
                     audio_state.session ? vivid_poc::session_track_name(audio_state.session, 0) : "none — test tone");
        if (ma_device_start(&device) != MA_SUCCESS) audio_ok = false;
    }
    glfwSetWindowUserPointer(window, &audio_state);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    std::fprintf(stderr, "[vivid] audio: %s (%u Hz)\n",
                 audio_ok ? "running" : "unavailable", audio_ok ? device.sampleRate : 0);

    // MCP control server: a loopback HTTP endpoint the agent bridge drives. Commands
    // are queued on the HTTP thread and applied on the main thread each frame.
    vivid::ControlServer control;
    vivid::ControlCtx cctx{ audio_state.session, &graph, &vgraph, &transport,
                            &g_win_w, &g_win_h, &g_split_x, &g_dock_h };
    { const char* pe = std::getenv("VIVID_PORT"); control.start(pe ? std::atoi(pe) : 9876); }

    float react = 0.f;   // smoothed master level
    float trHold = 0.f;  // peak-held transient (so onsets are visible at frame rate)
    float trkReact[8] = {0}, trkTrHold[8] = {0};  // per-track smoothed level / held transient

    // Event polling is split from rendering and driven by a CFRunLoopTimer (see
    // macos_run_frame_loop): rendering must keep firing while macOS runs a nested
    // tracking run-loop (the one a hosted plugin GUI enters on mouse-down), and a
    // plain glfwPollEvents busy-loop never services that nested loop — so plugin
    // editor windows render but ignore clicks. The timer fires in tracking mode too.
    auto poll_events = [&]() -> bool {
        glfwPollEvents();
        return !glfwWindowShouldClose(window);
    };
    auto tick = [&]() -> bool {
        if (glfwWindowShouldClose(window)) return false;
        cctx.session = audio_state.session;
        control.process_pending(cctx);   // apply queued MCP commands on the main thread

        // Resizable shell: reconfigure the surface (at framebuffer res) on resize.
        { int fbw = 0, fbh = 0; glfwGetFramebufferSize(window, &fbw, &fbh);
          if (fbw > 0 && fbh > 0 && (static_cast<uint32_t>(fbw) != gpu.width() || static_cast<uint32_t>(fbh) != gpu.height())) {
              gpu.resize(static_cast<uint32_t>(fbw), static_cast<uint32_t>(fbh));
              int ww = 0, wh = 0; glfwGetWindowSize(window, &ww, &wh);
              g_win_w = ww > 0 ? ww : fbw; g_win_h = wh > 0 ? wh : fbh;
              g_fb_w = fbw; g_fb_h = fbh;
              g_dpi = (g_win_w > 0) ? static_cast<float>(g_fb_w) / static_cast<float>(g_win_w) : 1.0f;
          }
          g_split_x = std::clamp(g_split_x, 40.f, static_cast<float>(g_win_w) - 40.f);
          clip_editor.set_window(static_cast<float>(g_win_w), static_cast<float>(g_win_h));
        }

        // reap plugin editor windows the user closed (instruments + effects)
        for (int t = 0; audio_state.session && t < vivid_poc::session_track_count(audio_state.session); ++t)
            if (audio_state.track_win[t] && !vst3_plugin_window_is_open(audio_state.track_win[t])) {
                vst3_plugin_window_close(audio_state.track_win[t]); audio_state.track_win[t] = nullptr;
            }
        for (int k = 0; k < 8; ++k)
            if (audio_state.fx_win[k] && !vst3_plugin_window_is_open(audio_state.fx_win[k])) {
                vst3_plugin_window_close(audio_state.fx_win[k]); audio_state.fx_win[k] = nullptr;
            }

        const double beats = transport.beats.load(std::memory_order_relaxed);
        const float level = transport.level.load(std::memory_order_relaxed);
        react += (std::min(1.0f, level * 5.0f) - react) * 0.3f;            // smoothed level
        trHold *= 0.85f;                                                   // decay the held peak
        trHold = std::max(trHold, transport.transient.load(std::memory_order_relaxed));
        graph.set_value(0, react);                 // master level
        graph.set_value(1, std::min(1.0f, trHold)); // master transient
        graph.set_value(2, std::min(1.0f, transport.band_low.load(std::memory_order_relaxed) * 5.0f));
        graph.set_value(3, std::min(1.0f, transport.band_mid.load(std::memory_order_relaxed) * 8.0f));
        graph.set_value(4, std::min(1.0f, transport.band_high.load(std::memory_order_relaxed) * 12.0f));
        for (int t = 0; audio_state.session && t < vivid_poc::session_track_count(audio_state.session) && t < 8; ++t) {
            const float lv = vivid_poc::session_track_level(audio_state.session, t);
            trkReact[t] += (std::min(1.0f, lv * 5.0f) - trkReact[t]) * 0.3f;
            trkTrHold[t] *= 0.85f;
            trkTrHold[t] = std::max(trkTrHold[t], vivid_poc::session_track_transient(audio_state.session, t));
            graph.set_value(char_id_for(t, 0), trkReact[t]);
            graph.set_value(char_id_for(t, 1), std::min(1.0f, trkTrHold[t]));
            graph.set_value(char_id_for(t, 2), std::min(1.0f, vivid_poc::session_track_band(audio_state.session, t, 0) * 5.0f));
            graph.set_value(char_id_for(t, 3), std::min(1.0f, vivid_poc::session_track_band(audio_state.session, t, 1) * 8.0f));
            graph.set_value(char_id_for(t, 4), std::min(1.0f, vivid_poc::session_track_band(audio_state.session, t, 2) * 12.0f));
        }
        // Resolve each visual node's params from the registry (writes into the
        // VisualGraph nodes) and publish the viz.* return-path sources.
        graph.apply_params();
        // Apply any source -> audio-param mappings ("param:T:D:I").
        if (audio_state.session)
            for (const auto& m : graph.mappings()) {
                if (m.dest.rfind("param:", 0) != 0) continue;
                int T = -1, D = 0, I = 0;
                if (std::sscanf(m.dest.c_str(), "param:%d:%d:%d", &T, &D, &I) == 3 && T >= 0)
                    vivid_poc::session_set_param(audio_state.session, T, D,
                                                 vivid_poc::session_param_id(audio_state.session, T, D, I),
                                                 graph.dest_value(m.dest));
            }

        double mx, my; glfwGetCursorPos(window, &mx, &my);
        if (audio_state.split_drag)  // continue a splitter drag (either pane can collapse)
            g_split_x = std::clamp(static_cast<float>(mx), 40.f, static_cast<float>(g_win_w) - 40.f);
        if (audio_state.dock_drag)   // continue a device-dock resize
            g_dock_h = std::clamp(static_cast<float>(g_win_h) - static_cast<float>(my), 120.f, g_win_h * 0.5f);
        graph.on_move(mx, my);  // continue any node/wire drag
        if (clip_editor.is_open()) {           // continue a drag, commit edits
            clip_editor.on_move(mx, my);
            if (clip_editor.take_dirty() && audio_state.session) {
                if (clip_editor.is_audio()) {
                    float a, b; clip_editor.audio_trim(a, b);
                    vivid_poc::session_set_audio_trim(audio_state.session, clip_editor.track(), clip_editor.scene(), a, b);
                } else {
                    const auto& nv = clip_editor.notes();
                    vivid_poc::session_set_clip(audio_state.session, clip_editor.track(), clip_editor.scene(),
                                                nv.data(), static_cast<int>(nv.size()), clip_editor.length());
                }
            }
        }
        if (audio_state.gain_drag >= 0 && audio_state.session) {  // continue a mixer gain drag
            const Rect gr = track_gain_rect(audio_state.gain_drag, vivid_poc::session_scene_count(audio_state.session));
            vivid_poc::session_set_track_gain(audio_state.session, audio_state.gain_drag,
                                              std::min(1.0, std::max(0.0, (mx - gr.x) / gr.w)));
        }
        if (audio_state.param_drag >= 0) {  // continue a knob drag (vertical)
            const float v = std::clamp(audio_state.param_drag_v0 +
                                       static_cast<float>(audio_state.param_drag_y0 - my) * 0.006f, 0.f, 1.f);
            if (audio_state.param_is_node) {       // selected visual node's base param
                if (audio_state.graph) audio_state.graph->set_op_param_base_at(
                    audio_state.graph->selected_op(), audio_state.param_drag, v);
            } else if (audio_state.session) {      // audio device param
                const int ntr = vivid_poc::session_track_count(audio_state.session);
                const int seltr = std::min(std::max(audio_state.sel_track, 0), ntr - 1);
                const int seldev = std::max(0, audio_state.sel_device);
                vivid_poc::session_set_param(audio_state.session, seltr, seldev,
                                             vivid_poc::session_param_id(audio_state.session, seltr, seldev, audio_state.param_drag), v);
            }
        }

        vivid::FrameState frame;
        if (gpu.begin_frame(frame)) {
            const float tsec = static_cast<float>(glfwGetTime());
            // the generator (set by V or the generator node) drives the video source
            g_visual_source = (vgraph.generator() == vivid::VOp::Video) ? 1 : 0;
            if (g_video) video_play(g_video, g_visual_source == 1);
            // pull the latest video frame into the source texture (if showing video)
            if (g_visual_source == 1 && g_video) {
                const uint8_t* px = nullptr; uint32_t vw = 0, vh = 0;
                if (video_next_frame(g_video, &px, &vw, &vh) && px) {
                    if (vw != srcTex.w || vh != srcTex.h) {
                        srcTex.release();
                        srcTex.init(gpu.device(), vw, vh, gpu.surface_format());
                    }
                    srcTex.upload(gpu.queue(), px);
                }
            }
            // composable visuals chain -> viewer (per-node params, set by apply_params)
            clear_pass(frame.encoder, frame.view, 0.045f, 0.05f, 0.06f);  // static dark backdrop
            const Rect vp = viewer_rect();
            vgraph.render(frame.encoder, frame.view, vp.x * g_dpi, vp.y * g_dpi, vp.w * g_dpi, vp.h * g_dpi, tsec, srcTex.view);

            draw_ui(ui, audio_state, beats, mx, my, g_win_w, g_win_h, g_split_x, g_dock_h, g_visual_source);
            const Rect vrp = viewer_rect();
            graph.set_bounds(g_split_x + 8.f, vrp.y + vrp.h + 16.f,
                             static_cast<float>(g_win_w) - 8.f, dock_top() - 8.f);
            graph.draw(ui);   // includes live node thumbnails via draw_texture
            draw_device_dock(ui, audio_state, mx, my, g_win_w, g_win_h, g_dock_h);   // bottom device-view dock (full width)
            // Pass 1: DAW + node graph (cards + thumbnails composite in-batch).
            ui.flush(frame.encoder, frame.view, g_win_w, g_win_h, g_fb_w, g_fb_h);
            // Pass 2: floating overlays — drawn AFTER pass 1 so they sit on top.
            graph.draw_overlays(ui);  // operator chooser
            draw_menu(ui, audio_state.menu,
                      audio_state.menu.src < 0 ? "Master"
                      : (audio_state.session ? vivid_poc::session_track_name(audio_state.session, audio_state.menu.src) : "track"));
            draw_fx_menu(ui, audio_state.fx_menu);
            draw_map_menu(ui, audio_state.map_menu);
            clip_editor.draw(ui);  // editor window on top
            if (g_show_mappings) draw_mapping_overview(ui, audio_state.graph, audio_state.session, g_win_w, g_win_h);
            ui.flush(frame.encoder, frame.view, g_win_w, g_win_h, g_fb_w, g_fb_h);
            gpu.end_frame(frame);
        }
        return true;
    };
    vivid::macos_run_frame_loop(poll_events, tick);

    control.stop();   // stop the MCP control server thread before tearing down state
    if (audio_ok) ma_device_uninit(&device);  // stops the callback first
    for (int t = 0; t < 8; ++t) if (audio_state.track_win[t]) vst3_plugin_window_close(audio_state.track_win[t]);
    for (int k = 0; k < 8; ++k) if (audio_state.fx_win[k]) vst3_plugin_window_close(audio_state.fx_win[k]);
    if (audio_state.session) vivid_poc::session_destroy(audio_state.session);
    if (g_video) { video_close(g_video); g_video = nullptr; }
    vgraph.shutdown();
    srcTex.release();
    ui.shutdown();
    gpu.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
