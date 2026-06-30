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
#include "platform/macos_frame_timer.h"

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

        // reap plugin editor windows the user closed (instruments + effects)
        for (int t = 0; app.session && t < vivid_poc::session_track_count(app.session); ++t)
            if (win.track_win[t] && !vst3_plugin_window_is_open(win.track_win[t])) {
                vst3_plugin_window_close(win.track_win[t]); win.track_win[t] = nullptr;
            }
        for (int k = 0; k < 8; ++k)
            if (win.fx_win[k] && !vst3_plugin_window_is_open(win.fx_win[k])) {
                vst3_plugin_window_close(win.fx_win[k]); win.fx_win[k] = nullptr;
            }

        const double beats = transport.beats.load(std::memory_order_relaxed);
        const float level = transport.level.load(std::memory_order_relaxed);
        win.react += (std::min(1.0f, level * 5.0f) - win.react) * 0.3f;        // smoothed level
        win.trHold *= 0.85f;                                                   // decay the held peak
        win.trHold = std::max(win.trHold, transport.transient.load(std::memory_order_relaxed));
        graph.set_value(0, win.react);                 // master level
        graph.set_value(1, std::min(1.0f, win.trHold)); // master transient
        graph.set_value(2, std::min(1.0f, transport.band_low.load(std::memory_order_relaxed) * 5.0f));
        graph.set_value(3, std::min(1.0f, transport.band_mid.load(std::memory_order_relaxed) * 8.0f));
        graph.set_value(4, std::min(1.0f, transport.band_high.load(std::memory_order_relaxed) * 12.0f));
        for (int t = 0; app.session && t < vivid_poc::session_track_count(app.session) && t < 8; ++t) {
            const float lv = vivid_poc::session_track_level(app.session, t);
            win.trkReact[t] += (std::min(1.0f, lv * 5.0f) - win.trkReact[t]) * 0.3f;
            win.trkTrHold[t] *= 0.85f;
            win.trkTrHold[t] = std::max(win.trkTrHold[t], vivid_poc::session_track_transient(app.session, t));
            graph.set_value(char_id_for(t, 0), win.trkReact[t]);
            graph.set_value(char_id_for(t, 1), std::min(1.0f, win.trkTrHold[t]));
            graph.set_value(char_id_for(t, 2), std::min(1.0f, vivid_poc::session_track_band(app.session, t, 0) * 5.0f));
            graph.set_value(char_id_for(t, 3), std::min(1.0f, vivid_poc::session_track_band(app.session, t, 1) * 8.0f));
            graph.set_value(char_id_for(t, 4), std::min(1.0f, vivid_poc::session_track_band(app.session, t, 2) * 12.0f));
        }
        // Resolve each visual node's params from the registry (writes into the
        // VisualGraph nodes) and publish the viz.* return-path sources.
        graph.apply_params();
        // Apply any source -> audio-param mappings ("param:T:D:I").
        if (app.session)
            for (const auto& m : graph.mappings()) {
                if (m.dest.rfind("param:", 0) != 0) continue;
                int T = -1, D = 0, I = 0;
                if (std::sscanf(m.dest.c_str(), "param:%d:%d:%d", &T, &D, &I) == 3 && T >= 0)
                    vivid_poc::session_set_param(app.session, T, D,
                                                 vivid_poc::session_param_id(app.session, T, D, I),
                                                 graph.dest_value(m.dest));
            }

        double mx, my; glfwGetCursorPos(window, &mx, &my);
        if (win.split_drag)  // continue a splitter drag (either pane can collapse)
            win.split_x = std::clamp(static_cast<float>(mx), 40.f, static_cast<float>(win.win_w) - 40.f);
        if (win.dock_drag)   // continue a device-dock resize
            win.dock_h = std::clamp(static_cast<float>(win.win_h) - static_cast<float>(my), 120.f, win.win_h * 0.5f);
        graph.on_move(mx, my);  // continue any node/wire drag
        if (clip_editor.is_open()) {           // continue a drag, commit edits
            clip_editor.on_move(mx, my);
            if (clip_editor.take_dirty() && app.session) {
                if (clip_editor.is_audio()) {
                    float a, b; clip_editor.audio_trim(a, b);
                    vivid_poc::session_set_audio_trim(app.session, clip_editor.track(), clip_editor.scene(), a, b);
                } else {
                    const auto& nv = clip_editor.notes();
                    vivid_poc::session_set_clip(app.session, clip_editor.track(), clip_editor.scene(),
                                                nv.data(), static_cast<int>(nv.size()), clip_editor.length());
                }
            }
        }
        if (win.gain_drag >= 0 && app.session) {  // continue a mixer gain drag
            const Rect gr = track_gain_rect(win.gain_drag, vivid_poc::session_scene_count(app.session));
            vivid_poc::session_set_track_gain(app.session, win.gain_drag,
                                              std::min(1.0, std::max(0.0, (mx - gr.x) / gr.w)));
        }
        if (win.param_drag >= 0) {  // continue a knob drag (vertical)
            const float v = std::clamp(win.param_drag_v0 +
                                       static_cast<float>(win.param_drag_y0 - my) * 0.006f, 0.f, 1.f);
            if (win.param_is_node) {       // selected visual node's base param
                if (app.graph) app.graph->set_op_param_base_at(app.graph->selected_op(), win.param_drag, v);
            } else if (app.session) {      // audio device param
                const int ntr = vivid_poc::session_track_count(app.session);
                const int seltr = std::min(std::max(win.sel_track, 0), ntr - 1);
                const int seldev = std::max(0, win.sel_device);
                vivid_poc::session_set_param(app.session, seltr, seldev,
                                             vivid_poc::session_param_id(app.session, seltr, seldev, win.param_drag), v);
            }
        }

        FrameState frame;
        if (gpu.begin_frame(frame)) {
            const float tsec = static_cast<float>(glfwGetTime());
            // the generator (set by V or the generator node) drives the video source
            app.visual_source = (vgraph.generator() == VOp::Video) ? 1 : 0;
            if (app.video) video_play(app.video, app.visual_source == 1);
            // pull the latest video frame into the source texture (if showing video)
            if (app.visual_source == 1 && app.video) {
                const uint8_t* px = nullptr; uint32_t vw = 0, vh = 0;
                if (video_next_frame(app.video, &px, &vw, &vh) && px) {
                    if (vw != srcTex.w || vh != srcTex.h) {
                        srcTex.release();
                        srcTex.init(gpu.device(), vw, vh, gpu.surface_format());
                    }
                    srcTex.upload(gpu.queue(), px);
                }
            }
            // composable visuals chain -> viewer (per-node params, set by apply_params)
            clear_pass(frame.encoder, frame.view, 0.045f, 0.05f, 0.06f);  // static dark backdrop
            const Rect vp = win.viewer_rect();
            vgraph.render(frame.encoder, frame.view, vp.x * win.dpi, vp.y * win.dpi, vp.w * win.dpi, vp.h * win.dpi, tsec, srcTex.view);

            draw_ui(ui, win, beats, mx, my);
            const Rect vrp = win.viewer_rect();
            graph.set_bounds(win.split_x + 8.f, vrp.y + vrp.h + 16.f,
                             static_cast<float>(win.win_w) - 8.f, win.dock_top() - 8.f);
            graph.draw(ui);   // includes live node thumbnails via draw_texture
            draw_device_dock(ui, win, mx, my);   // bottom device-view dock (full width)
            // Pass 1: DAW + node graph (cards + thumbnails composite in-batch).
            ui.flush(frame.encoder, frame.view, win.win_w, win.win_h, win.fb_w, win.fb_h);
            // Pass 2: floating overlays — drawn AFTER pass 1 so they sit on top.
            graph.draw_overlays(ui);  // operator chooser
            draw_menu(ui, win.menu,
                      win.menu.src < 0 ? "Master"
                      : (app.session ? vivid_poc::session_track_name(app.session, win.menu.src) : "track"));
            draw_fx_menu(ui, win.fx_menu);
            draw_map_menu(ui, win.map_menu);
            clip_editor.draw(ui);  // editor window on top
            if (win.show_mappings) draw_mapping_overview(ui, app.graph, app.session, win.win_w, win.win_h);
            ui.flush(frame.encoder, frame.view, win.win_w, win.win_h, win.fb_w, win.fb_h);
            gpu.end_frame(frame);
        }
        return true;
    };
    macos_run_frame_loop(poll_events, tick);
}

}  // namespace vivid
