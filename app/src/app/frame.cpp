#include "app/frame.h"

#include "app/app.h"
#include "app/window.h"
#include "app/editor_window.h"   // UI-5: floated operator-editor window
#include "app/window_prefs.h"    // UI-5.4c: remembered float-window geometry
#include "gpu/gpu_context.h"
#include "gpu/gpu_util.h"
#include "ui/renderer_2d.h"
#include "ui/node_graph.h"
#include "ui/layout.h"
#include "ui/compound_widget.h"   // UI-4a: compound_span / xy_from_cursor / node_param_compound_rect
#include "ui/session_view.h"
#include "ui/mapping_overview.h"
#include "ui/clip_editor.h"
#include "transport.h"
#include "audio/vst3_host.h"
#include "audio/vst3_plugin_window.h"
#include "gpu/visual_graph.h"
#include "gpu/texture_source.h"
#include "gpu/video_player.h"
#include "cli/control_server.h"
#include "platform/frame_loop.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid {

// Esc closes the pop-out visuals window (the tick reaps it on shouldClose).
static void popout_key_callback(GLFWwindow* w, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(w, GLFW_TRUE);
}

// Open/close the pop-out visuals window: fullscreen on a second monitor if one is
// present, else a large windowed view. Shares the wgpu device via a secondary surface.
void toggle_popout(App& app, Window& win) {
    if (!app.gpu) return;
    if (win.popout) {   // close
        app.gpu->close_secondary();
        glfwDestroyWindow(win.popout);
        win.popout = nullptr; win.popout_fb_w = win.popout_fb_h = 0;
        return;
    }
    int mcount = 0; GLFWmonitor** mons = glfwGetMonitors(&mcount);
    GLFWmonitor* mon = (mcount > 1) ? mons[1] : nullptr;   // performance screen = the 2nd monitor
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* w = mon ? [&]{ const GLFWvidmode* vm = glfwGetVideoMode(mon);
                               return glfwCreateWindow(vm->width, vm->height, "Vivid \xE2\x80\x94 Visuals", mon, nullptr); }()
                        : glfwCreateWindow(1280, 720, "Vivid \xE2\x80\x94 Visuals", nullptr, nullptr);
    if (!w) return;
    int fbw = 0, fbh = 0; glfwGetFramebufferSize(w, &fbw, &fbh);
    if (fbw <= 0 || fbh <= 0 || !app.gpu->open_secondary(w, static_cast<uint32_t>(fbw), static_cast<uint32_t>(fbh))) {
        glfwDestroyWindow(w); return;
    }
    glfwSetKeyCallback(w, popout_key_callback);
    win.popout = w; win.popout_fb_w = fbw; win.popout_fb_h = fbh;
}
namespace {

using namespace vivid::ui;  // Rect/hit, geometry, constants (ui/layout.h)

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

}  // namespace

void reap_plugin_windows(App& app, Window& win) {
    for (int t = 0; app.session && t < vivid::session::session_track_count(app.session); ++t)
        if (win.track_win[t] && !vst3_plugin_window_is_open(win.track_win[t])) {
            vst3_plugin_window_close(win.track_win[t]); win.track_win[t] = nullptr;
        }
    for (int k = 0; k < vivid::session::kMaxTracks; ++k)
        if (win.fx_win[k] && !vst3_plugin_window_is_open(win.fx_win[k])) {
            vst3_plugin_window_close(win.fx_win[k]); win.fx_win[k] = nullptr;
        }
}

void publish_bridge_sources(App& app, Window& win) {
    if (!app.transport || !app.graph) return;
    Transport& transport = *app.transport;
    NodeGraph& graph = *app.graph;
    const float level = transport.level.load(std::memory_order_relaxed);
    win.react += (std::min(1.0f, level * 5.0f) - win.react) * 0.3f;
    win.trHold *= 0.85f;
    win.trHold = std::max(win.trHold, transport.transient.load(std::memory_order_relaxed));
    graph.set_value(0, win.react);
    graph.set_value(1, std::min(1.0f, win.trHold));
    graph.set_value(2, std::min(1.0f, transport.band_low.load(std::memory_order_relaxed) * 5.0f));
    graph.set_value(3, std::min(1.0f, transport.band_mid.load(std::memory_order_relaxed) * 8.0f));
    graph.set_value(4, std::min(1.0f, transport.band_high.load(std::memory_order_relaxed) * 12.0f));
    for (int t = 0; app.session && t < vivid::session::session_track_count(app.session) && t < vivid::session::kMaxTracks; ++t) {
        const float lv = vivid::session::session_track_level(app.session, t);
        win.trkReact[t] += (std::min(1.0f, lv * 5.0f) - win.trkReact[t]) * 0.3f;
        win.trkTrHold[t] *= 0.85f;
        win.trkTrHold[t] = std::max(win.trkTrHold[t], vivid::session::session_track_transient(app.session, t));
        const int tid = vivid::session::session_track_id(app.session, t);
        graph.set_value(char_id_for(tid, 0), win.trkReact[t]);
        graph.set_value(char_id_for(tid, 1), std::min(1.0f, win.trkTrHold[t]));
        graph.set_value(char_id_for(tid, 2), std::min(1.0f, vivid::session::session_track_band(app.session, t, 0) * 5.0f));
        graph.set_value(char_id_for(tid, 3), std::min(1.0f, vivid::session::session_track_band(app.session, t, 1) * 8.0f));
        graph.set_value(char_id_for(tid, 4), std::min(1.0f, vivid::session::session_track_band(app.session, t, 2) * 12.0f));
    }
    graph.apply_params();
}

void apply_audio_param_mappings(App& app) {
    if (!app.session || !app.graph) return;
    for (const auto& m : app.graph->mappings()) {
        int T = -1, D = 0, I = 0;
        if (m.dest.rfind("param:", 0) == 0) {                 // VST3 device param (normalized)
            if (std::sscanf(m.dest.c_str(), "param:%d:%d:%d", &T, &D, &I) == 3 && T >= 0)
                vivid::session::session_set_param(app.session, T, D,
                                             vivid::session::session_param_id(app.session, T, D, I),
                                             app.graph->dest_value(m.dest));
        } else if (m.dest.rfind("aparam:", 0) == 0) {         // native audio-op param (IDX=-1 instrument)
            if (std::sscanf(m.dest.c_str(), "aparam:%d:%d:%d", &T, &D, &I) == 3 && T >= 0) {
                const float lo = vivid::session::session_audio_op_param_min(app.session, T, D, I);
                const float hi = vivid::session::session_audio_op_param_max(app.session, T, D, I);
                vivid::session::session_audio_op_param_set(app.session, T, D, I,
                                             lo + std::clamp(app.graph->dest_value(m.dest), 0.f, 1.f) * (hi - lo));
            }
        }
    }
}

void update_drag_continuations(App& app, Window& win, double mx, double my) {
    win.cur_x = mx; win.cur_y = my;   // latest cursor (used by the audio-graph ghost wire)
    if (win.ag_panning) {   // 2i: drag empty space in the audio graph to pan the view
        win.ag_pan_x = win.ag_pan_ox0 + static_cast<float>(mx - win.ag_pan_mx0);
        win.ag_pan_y = win.ag_pan_oy0 + static_cast<float>(my - win.ag_pan_my0);
    }
    if ((win.clip_drag_t >= 0 || win.clip_drag_from_pool >= 0) && !win.clip_dragging) {   // clip drag crosses the move threshold
        const double dx = mx - win.clip_drag_x0, dy = my - win.clip_drag_y0;
        if (dx * dx + dy * dy > 25.0) win.clip_dragging = true;   // ~5px
    }
    if (win.plugin_drag_i >= 0 && !win.plugin_dragging) {   // plugin drag crosses the move threshold
        const double dx = mx - win.plugin_drag_x0, dy = my - win.plugin_drag_y0;
        if (dx * dx + dy * dy > 25.0) win.plugin_dragging = true;
    }
    if (win.split_drag)
        win.split_x = std::clamp(static_cast<float>(mx), 40.f, static_cast<float>(win.win_w) - 40.f);
    if (win.dock_drag)
        win.dock_h = std::clamp(static_cast<float>(win.win_h) - static_cast<float>(my), 120.f, win.win_h * 0.5f);
    if (app.graph) app.graph->on_move(mx, my);
    if (win.editor && win.editor->is_open()) {
        win.editor->on_move(mx, my);
        if (win.editor->take_dirty() && app.session) {
            if (win.editor->is_audio()) {
                float a, b; win.editor->audio_trim(a, b);
                vivid::session::session_set_audio_trim(app.session, win.editor->track(), win.editor->scene(), a, b);
            } else {
                const auto& nv = win.editor->notes();
                vivid::session::session_set_clip(app.session, win.editor->track(), win.editor->scene(),
                                            nv.data(), static_cast<int>(nv.size()), win.editor->length());
            }
        }
        if (!win.editor->is_audio() && app.session && win.editor->take_loop_dirty()) {   // in-clip loop edit
            double ls, le; win.editor->loop_range(ls, le);
            vivid::session::session_set_clip_loop(app.session, win.editor->track(), win.editor->scene(), ls, le);
        }
        // A5: apply audio warp/pitch/auto-warp requests from the editor header, then refresh
        // the editor's marker + shape display from the engine.
        if (win.editor->is_audio() && app.session) {
            const int req = win.editor->take_audio_req();   // 1 shaping, 2 auto, 4 warp-pts, 8 slice
            if (req) {
                namespace S = vivid::session;
                const int tk = win.editor->track(), scn = win.editor->scene();
                if (req & 2) S::session_audio_auto_warp(app.session, tk, scn, 0.5f);
                if (req & 1) { const int m = win.editor->audio_warp_mode();
                               S::session_set_audio_warp(app.session, tk, scn, m >= 0 ? 1 : 0, m < 0 ? 0 : m);
                               S::session_set_audio_pitch(app.session, tk, scn, win.editor->audio_pitch()); }
                if (req & 4) S::session_audio_set_warp_pts(app.session, tk, scn, win.editor->warp_samples().data(),
                                                           win.editor->warp_beats().data(), static_cast<int>(win.editor->warp_samples().size()));
                if (req & 8) { float sl[64]; const int ns = S::session_audio_slices(app.session, tk, scn, win.editor->audio_slice_mode(), sl, 64);
                               win.editor->set_slices(sl, ns); }
                if (req & 16) {   // A6: slice this clip into a new Sampler-driven MIDI track
                    const int m = win.editor->audio_slice_mode();
                    S::session_slice_to_midi(app.session, tk, scn, m > 0 ? m : 1);
                }
                if (req & (1 | 2 | 4)) {   // reload the marker + shape display from the engine
                    float wp[256], tr[512]; double wb[256];
                    const int nw = S::session_audio_get_warp_pts(app.session, tk, scn, wp, 256);
                    S::session_audio_get_warp_beats(app.session, tk, scn, wb, 256);
                    const int ntr = S::session_audio_get_transients(app.session, tk, scn, tr, 512);
                    win.editor->set_audio_markers(wp, wb, nw, tr, ntr);
                    win.editor->set_audio_shape(S::session_get_audio_warp(app.session, tk, scn),
                                                S::session_get_audio_pitch(app.session, tk, scn));
                }
            }
        }
    }
    if (win.gain_drag >= 0 && app.session) {
        const Rect gr = track_gain_rect(win.gain_drag, vivid::session::session_scene_count(app.session));
        vivid::session::session_set_track_gain(app.session, win.gain_drag,
                                          std::min(1.0, std::max(0.0, (mx - win.sidebar_w - gr.x) / gr.w)));
    }
    if (win.param_drag >= 0) {
        if (win.param_xy && app.graph) {   // UI-4a: XY-pad — both axes track the cursor in the pad rect
            const int span = vivid::ui::compound_span(VIVID_DISPLAY_XY_PAD);
            const vivid::ui::Rect cr = vivid::ui::node_param_compound_rect(win.param_drag, span, win.win_w, win.win_h, win.dock_h);
            float x01, y01; vivid::ui::xy_from_cursor(cr, mx, my, x01, y01);
            app.graph->set_op_param_base_at(app.graph->selected_op(), win.param_drag, x01);
            app.graph->set_op_param_base_at(app.graph->selected_op(), win.param_drag + 1, y01);
            return;
        }
        if (win.param_is_node && win.param_drag_horiz) {   // node slider: horizontal position = value
            const vivid::ui::Rect wr = vivid::ui::node_param_widget_rect(win.param_drag, win.win_w, win.win_h, win.dock_h);
            if (app.graph) app.graph->set_op_param_base_at(app.graph->selected_op(), win.param_drag,
                                                           std::clamp((static_cast<float>(mx) - wr.x) / wr.w, 0.f, 1.f));
            return;
        }
        const float v = std::clamp(win.param_drag_v0 +
                                   static_cast<float>(win.param_drag_y0 - my) * 0.006f, 0.f, 1.f);
        if (win.param_is_node) {
            if (app.graph) app.graph->set_op_param_base_at(app.graph->selected_op(), win.param_drag, v);
        } else if (app.session) {
            const int ntr = vivid::session::session_track_count(app.session);
            const int seltr = std::min(std::max(win.sel_track, 0), ntr - 1);
            const vivid::ui::DevSlot seldev = vivid::ui::dock_resolve(app.session, seltr, std::max(0, win.sel_device));
            vivid::ui::dock_param_set_norm(app.session, seltr, seldev, win.param_drag, v);
        }
    }
    // UI-3 Stage 1: drag a selected audio-graph node's param knob (vertical). ag_param_v0 is the
    // normalized value at drag start; convert back to the op's [min,max] to set it.
    if (win.ag_param_drag >= 0 && app.session && win.sel_audio_node >= 0) {
        namespace S = vivid::session;
        const int tr = std::min(std::max(win.sel_track, 0), S::session_track_count(app.session) - 1);
        const int nid = win.sel_audio_node;   // node id (not chain index)
        const float mn = S::session_audio_graph_node_param_min(app.session, tr, nid, win.ag_param_drag);
        const float mxx = S::session_audio_graph_node_param_max(app.session, tr, nid, win.ag_param_drag);
        const float norm = std::clamp(win.ag_param_v0 + static_cast<float>(win.ag_param_y0 - my) * 0.006f, 0.f, 1.f);
        S::session_audio_graph_node_param_set(app.session, tr, nid, win.ag_param_drag, mn + norm * (mxx - mn));
    }
}

void update_visual_source_frame(App& app) {
    if (!app.vgraph || !app.gpu || !app.srcTex) return;
    app.visual_source = (app.vgraph->generator() == VOp::Video) ? 1 : 0;
    if (app.video) video_play(app.video, app.visual_source == 1);
    if (app.visual_source == 1 && app.video) {
        const uint8_t* px = nullptr; uint32_t vw = 0, vh = 0;
        if (video_next_frame(app.video, &px, &vw, &vh) && px) {
            if (vw != app.srcTex->w || vh != app.srcTex->h) {
                app.srcTex->release();
                app.srcTex->init(app.gpu->device(), vw, vh, app.gpu->surface_format());
            }
            app.srcTex->upload(app.gpu->queue(), px);
        }
    }
}

void run_frame_loop(App& app, Window& win) {
    // Local aliases to the shared engine (App) + this view (Window) so the tick
    // body reads naturally; every object is owned by main(), not here.
    GLFWwindow*    window      = win.glfw;
    GpuContext&    gpu         = *app.gpu;
    Renderer2D&    ui          = *win.ui;
    VisualGraph&   vgraph      = *app.vgraph;
    NodeGraph&     graph       = *app.graph;
    Transport&     transport   = *app.transport;
    ClipEditor&    clip_editor = *win.editor;
    TextureSource& srcTex      = *app.srcTex;
    ControlServer& control     = *app.control;

    ControlCtx cctx{ app.session, &graph, &vgraph, &transport, &app,
                     &win.win_w, &win.win_h, &win.split_x, &win.dock_h };

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
        cctx.session = app.session;
        control.process_pending(cctx);   // apply queued MCP commands on the main thread
        app.hot_reload.tick();           // apply any ready operator hot-swaps (main thread)

        // Hardware MIDI (M6.4): drain the input queue on the main thread and route to the
        // armed track's instrument (so all Session access stays on the UI thread).
        if (app.session) {
            vivid::platform::MidiEvent mev[64];
            const int nm = app.midi_in.poll(mev, 64);
            const bool step = win.editor && win.editor->is_open() && win.editor->step_mode();
            for (int i = 0; i < nm; ++i) {
                if (mev[i].on) {
                    vivid::session::session_note_on(app.session, mev[i].pitch, mev[i].vel);
                    if (step) win.editor->step_note_on(mev[i].pitch, mev[i].vel);
                } else {
                    vivid::session::session_note_off(app.session, mev[i].pitch);
                    if (step) win.editor->step_note_off();
                }
            }
        }

        // Resizable shell: reconfigure the surface (at framebuffer res) on resize.
        { int fbw = 0, fbh = 0; glfwGetFramebufferSize(window, &fbw, &fbh);
          if (fbw > 0 && fbh > 0 && (static_cast<uint32_t>(fbw) != gpu.width() || static_cast<uint32_t>(fbh) != gpu.height())) {
              gpu.resize(static_cast<uint32_t>(fbw), static_cast<uint32_t>(fbh));
              int ww = 0, wh = 0; glfwGetWindowSize(window, &ww, &wh);
              win.win_w = ww > 0 ? ww : fbw; win.win_h = wh > 0 ? wh : fbh;
              win.fb_w = fbw; win.fb_h = fbh;
              win.dpi = (win.win_w > 0) ? static_cast<float>(win.fb_w) / static_cast<float>(win.win_w) : 1.0f;
          }
          win.split_x = std::clamp(win.split_x, 40.f, static_cast<float>(win.win_w) - 40.f);
          clip_editor.set_window(static_cast<float>(win.win_w), static_cast<float>(win.win_h));
          clip_editor.set_dock_h(win.dock_h);   // docked editor shares the bottom-dock height
        }

        reap_plugin_windows(app, win);
        const double beats = transport.beats.load(std::memory_order_relaxed);
        publish_bridge_sources(app, win);
        apply_audio_param_mappings(app);

        double mx, my; glfwGetCursorPos(window, &mx, &my);
        update_drag_continuations(app, win, mx, my);

        const float tsec = static_cast<float>(glfwGetTime());
        FrameState frame;
        if (gpu.begin_frame(frame)) {
            update_visual_source_frame(app);
            // composable visuals chain -> viewer (per-node params, set by apply_params)
            clear_pass(frame.encoder, frame.view, 0.045f, 0.05f, 0.06f);  // static dark backdrop
            const Rect vp = win.viewer_rect();
            vgraph.render(frame.encoder, frame.view, vp.x * win.dpi, vp.y * win.dpi, vp.w * win.dpi, vp.h * win.dpi, tsec, srcTex.view);

            draw_ui(ui, win, beats, mx, my);
            // UI-2: the visuals node graph is a deep view under the output — drawn only when
            // revealed (win.show_graph). When hidden, the output fills the visual zone and the
            // graph has no hit area (bounds zeroed) so it never consumes clicks.
            if (win.show_graph) {
                const Rect sig = win.signal_panel();   // node graph renders inside the SIGNAL region
                graph.set_bounds(sig.x + 8.f, sig.y + 26.f, sig.x + sig.w - 8.f, sig.y + sig.h - 8.f);
                graph.draw(ui);   // includes live node thumbnails via draw_texture
            } else {
                graph.set_bounds(0.f, 0.f, 0.f, 0.f);
            }
            // UI-1: recompute the detail region's explicit focus — the single source of truth
            // for what the bottom region shows + its domain — replacing the old implicit race
            // where draw and input each re-derived the mode from the current selection.
            {
                FocusContext f;
                const int selop = app.graph ? app.graph->selected_op() : -1;
                if (clip_editor.is_open() && clip_editor.is_docked()) {
                    f.kind = FocusContext::Kind::ClipEditor; f.dom = FocusContext::Dom::Audio;
                    f.track = clip_editor.track(); f.scene = clip_editor.scene();
                } else if (win.show_audio_graph) {   // UI-3: audio graph deep view (drilled in from a track)
                    f.kind = FocusContext::Kind::AudioGraph; f.dom = FocusContext::Dom::Audio; f.track = win.sel_track;
                } else if (selop >= 0 && win.show_op_editor && app.graph->op_has_editor(selop)) {
                    f.kind = FocusContext::Kind::OpEditor; f.dom = FocusContext::Dom::Visual; f.node = selop;   // UI-4b
                } else if (selop >= 0) {
                    f.kind = FocusContext::Kind::VisualNode; f.dom = FocusContext::Dom::Visual; f.node = selop;
                } else {
                    f.kind = FocusContext::Kind::Device; f.dom = FocusContext::Dom::Audio; f.track = win.sel_track;
                }
                win.focus = f;
            }
            // The bottom dock is the detail region: the clip editor owns it while docked;
            // otherwise it shows the focused device (audio) or visual-node (visual) inspector.
            if (win.focus.kind != FocusContext::Kind::ClipEditor) draw_device_dock(ui, win, mx, my);
            // Pass 1: DAW + node graph (cards + thumbnails composite in-batch).
            ui.flush(frame.encoder, frame.view, win.win_w, win.win_h, win.fb_w, win.fb_h);
            // Pass 2: floating overlays — drawn AFTER pass 1 so they sit on top.
            if (win.show_graph) graph.draw_overlays(ui);  // operator chooser (graph deep view only)
            draw_menu(ui, win.menu,
                      win.menu.src < 0 ? "Master"
                      : (app.session ? vivid::session::session_track_name(app.session, win.menu.src) : "track"));
            draw_fx_menu(ui, app.session, win.fx_menu);
            draw_track_menu(ui, win.track_menu);
            draw_map_menu(ui, win.map_menu);
            draw_node_menu(ui, win);
            clip_editor.set_playhead(beats);
            clip_editor.draw(ui);  // editor window on top
            if (win.show_mappings) draw_mapping_overview(ui, app.graph, app.session, win.win_w, win.win_h);
            ui.flush(frame.encoder, frame.view, win.win_w, win.win_h, win.fb_w, win.fb_h);
            gpu.end_frame(frame);
        }

        // Pop-out visuals window: mirror the current output onto its surface, fullscreen.
        // (The graph rendered once above; this only re-blits the output FBO.)
        if (win.popout) {
            if (glfwWindowShouldClose(win.popout)) { toggle_popout(app, win); }
            else if (gpu.has_secondary()) {
                int fbw = 0, fbh = 0; glfwGetFramebufferSize(win.popout, &fbw, &fbh);
                if (fbw > 0 && fbh > 0) {
                    if (fbw != win.popout_fb_w || fbh != win.popout_fb_h) {
                        gpu.resize_secondary(static_cast<uint32_t>(fbw), static_cast<uint32_t>(fbh));
                        win.popout_fb_w = fbw; win.popout_fb_h = fbh;
                    }
                    FrameState f2;
                    if (gpu.begin_secondary(f2)) {
                        vgraph.present_to(f2.encoder, f2.view, 0.f, 0.f, static_cast<float>(fbw), static_cast<float>(fbh), tsec);
                        gpu.end_secondary(f2);
                    }
                }
            }
        }

        // UI-5: floated operator-editor window. Opened here (deferred out of the input callback so
        // window creation happens on the main thread between polls), rendered every frame, and torn
        // down when it wants to close (user closed it / operator asked / node lost its editor).
        if (win.want_float_node >= 0) {
            if (!win.editor_win && app.graph && app.graph->op_has_editor(win.want_float_node)) {
                auto* ew = new EditorWindow();
                const VividEditorMetadata m = app.graph->op_editor_metadata(win.want_float_node);
                std::string title = std::string("Vivid \xE2\x80\x94 ") + app.graph->op_kind_name(win.want_float_node)
                                  + (m.title_suffix ? m.title_suffix : "");
                // UI-5.4c: reopen at the remembered size/position (clamped to the op's min), else the
                // metadata default.
                const WindowPrefs gp = load_window_prefs(editor_window_prefs_path());
                const int ow = gp.has_size ? std::max<int>(gp.w, (int)m.min_width)  : (int)m.default_width;
                const int oh = gp.has_size ? std::max<int>(gp.h, (int)m.min_height) : (int)m.default_height;
                if (ew->open(app, win.want_float_node, title, ow, oh)) {
                    if (gp.has_pos && ew->glfw()) glfwSetWindowPos(ew->glfw(), gp.x, gp.y);
                    win.editor_win = ew;
                } else delete ew;
            }
            win.want_float_node = -1;
        }
        if (win.editor_win) {
            if (!win.editor_win->render(app)) { win.editor_win->close(app); delete win.editor_win; win.editor_win = nullptr; }
        }
        return true;
    };
    run_platform_frame_loop(poll_events, tick);
    if (win.editor_win) { win.editor_win->close(app); delete win.editor_win; win.editor_win = nullptr; }  // UI-5 teardown
}

}  // namespace vivid
