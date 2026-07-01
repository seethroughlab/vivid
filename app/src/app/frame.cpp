#include "app/frame.h"

#include "app/app.h"
#include "app/window.h"
#include "gpu/gpu_context.h"
#include "gpu/gpu_util.h"
#include "ui/renderer_2d.h"
#include "ui/node_graph.h"
#include "ui/layout.h"
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
        if (m.dest.rfind("param:", 0) != 0) continue;
        int T = -1, D = 0, I = 0;
        if (std::sscanf(m.dest.c_str(), "param:%d:%d:%d", &T, &D, &I) == 3 && T >= 0)
            vivid::session::session_set_param(app.session, T, D,
                                         vivid::session::session_param_id(app.session, T, D, I),
                                         app.graph->dest_value(m.dest));
    }
}

void update_drag_continuations(App& app, Window& win, double mx, double my) {
    if ((win.clip_drag_t >= 0 || win.clip_drag_from_pool >= 0) && !win.clip_dragging) {   // clip drag crosses the move threshold
        const double dx = mx - win.clip_drag_x0, dy = my - win.clip_drag_y0;
        if (dx * dx + dy * dy > 25.0) win.clip_dragging = true;   // ~5px
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
    }
    if (win.gain_drag >= 0 && app.session) {
        const Rect gr = track_gain_rect(win.gain_drag, vivid::session::session_scene_count(app.session));
        vivid::session::session_set_track_gain(app.session, win.gain_drag,
                                          std::min(1.0, std::max(0.0, (mx - win.sidebar_w - gr.x) / gr.w)));
    }
    if (win.param_drag >= 0) {
        const float v = std::clamp(win.param_drag_v0 +
                                   static_cast<float>(win.param_drag_y0 - my) * 0.006f, 0.f, 1.f);
        if (win.param_is_node) {
            if (app.graph) app.graph->set_op_param_base_at(app.graph->selected_op(), win.param_drag, v);
        } else if (app.session) {
            const int ntr = vivid::session::session_track_count(app.session);
            const int seltr = std::min(std::max(win.sel_track, 0), ntr - 1);
            const int seldev = std::max(0, win.sel_device);
            vivid::session::session_set_param(app.session, seltr, seldev,
                                         vivid::session::session_param_id(app.session, seltr, seldev, win.param_drag), v);
        }
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
        }

        reap_plugin_windows(app, win);
        const double beats = transport.beats.load(std::memory_order_relaxed);
        publish_bridge_sources(app, win);
        apply_audio_param_mappings(app);

        double mx, my; glfwGetCursorPos(window, &mx, &my);
        update_drag_continuations(app, win, mx, my);

        FrameState frame;
        if (gpu.begin_frame(frame)) {
            const float tsec = static_cast<float>(glfwGetTime());
            update_visual_source_frame(app);
            // composable visuals chain -> viewer (per-node params, set by apply_params)
            clear_pass(frame.encoder, frame.view, 0.045f, 0.05f, 0.06f);  // static dark backdrop
            const Rect vp = win.viewer_rect();
            vgraph.render(frame.encoder, frame.view, vp.x * win.dpi, vp.y * win.dpi, vp.w * win.dpi, vp.h * win.dpi, tsec, srcTex.view);

            draw_ui(ui, win, beats, mx, my);
            const Rect sig = win.signal_panel();   // node graph renders inside the SIGNAL region
            graph.set_bounds(sig.x + 8.f, sig.y + 26.f, sig.x + sig.w - 8.f, sig.y + sig.h - 8.f);
            graph.draw(ui);   // includes live node thumbnails via draw_texture
            draw_device_dock(ui, win, mx, my);   // bottom device-view dock (full width)
            // Pass 1: DAW + node graph (cards + thumbnails composite in-batch).
            ui.flush(frame.encoder, frame.view, win.win_w, win.win_h, win.fb_w, win.fb_h);
            // Pass 2: floating overlays — drawn AFTER pass 1 so they sit on top.
            graph.draw_overlays(ui);  // operator chooser
            draw_menu(ui, win.menu,
                      win.menu.src < 0 ? "Master"
                      : (app.session ? vivid::session::session_track_name(app.session, win.menu.src) : "track"));
            draw_fx_menu(ui, win.fx_menu);
            draw_track_menu(ui, win.track_menu);
            draw_map_menu(ui, win.map_menu);
            clip_editor.draw(ui);  // editor window on top
            if (win.show_mappings) draw_mapping_overview(ui, app.graph, app.session, win.win_w, win.win_h);
            ui.flush(frame.encoder, frame.view, win.win_w, win.win_h, win.fb_w, win.fb_h);
            gpu.end_frame(frame);
        }
        return true;
    };
    run_platform_frame_loop(poll_events, tick);
}

}  // namespace vivid
