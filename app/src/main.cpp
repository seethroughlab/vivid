// Vivid PoC — entry point. Constructs the shared engine (App) + a single view
// (Window), opens the audio device + MCP control server, and runs the macOS frame
// loop. The god-file UI/draw/input code now lives in cohesive modules under
// ui/, app/, and audio/ (see app/ARCHITECTURE notes); main() is just wiring + the
// per-frame orchestration. App = shared model (one per process); Window = per-view
// state — the seam that lets editor windows be added later.
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
#include "app/app.h"
#include "app/window.h"
#include "app/input.h"
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

int main() {
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // WebGPU owns the surface
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Vivid PoC — foundation", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "glfwCreateWindow failed\n"); glfwTerminate(); return 1; }

    vivid::App app;        // shared engine/document (one per process)
    vivid::Window win;     // this window's view + interaction state
    win.app = &app;
    win.glfw = window;

    // Retina/HiDPI: render at the framebuffer (physical) resolution; lay out the UI
    // in logical points. win.dpi bridges them (2.0 on retina) -> crisp text + shapes.
    glfwGetWindowSize(window, &win.win_w, &win.win_h);
    glfwGetFramebufferSize(window, &win.fb_w, &win.fb_h);
    win.dpi = (win.win_w > 0) ? static_cast<float>(win.fb_w) / static_cast<float>(win.win_w) : 1.0f;

    vivid::GpuContext gpu;
    if (!gpu.init(window, static_cast<uint32_t>(win.fb_w), static_cast<uint32_t>(win.fb_h))) {
        std::fprintf(stderr, "GpuContext init failed: %s\n", gpu.last_error().c_str());
        return 1;
    }
    app.gpu = &gpu;

    vivid::ui::Renderer2D ui;
    if (!ui.init(gpu.device(), gpu.surface_format(), VIVID_FONT_PATH, 15.0f, win.dpi))
        std::fprintf(stderr, "[vivid] Renderer2D init failed (UI disabled)\n");
    win.ui = &ui;

    // Composable visuals chain (generator -> feedback -> blur -> viewer).
    const uint32_t kRtW = static_cast<uint32_t>(kViewW), kRtH = static_cast<uint32_t>(kViewH);
    vivid::VisualGraph vgraph;
    if (!vgraph.init(gpu.device(), gpu.queue(), gpu.surface_format(), kRtW, kRtH))
        std::fprintf(stderr, "[vivid] visual graph init failed (viewer disabled)\n");
    app.vgraph = &vgraph;

    // Texture source (image/video) — seeded with a test pattern; P19b feeds video.
    vivid::TextureSource srcTex;
    srcTex.init(gpu.device(), 512, 288, gpu.surface_format());
    { auto pat = vivid::gen_test_pattern(512, 288); srcTex.upload(gpu.queue(), pat.data()); }
    app.srcTex = &srcTex;

    // Scan the video folder and open the first clip (N cycles, V shows it).
    {
        const char* dir = "/Users/jeff/Movies/Gero Individual Reel Files";
        if (DIR* d = opendir(dir)) {
            while (dirent* e = readdir(d)) {
                std::string n = e->d_name;
                if (n.size() > 4 && n.compare(n.size() - 4, 4, ".mp4") == 0)
                    app.video_paths.push_back(std::string(dir) + "/" + n);
            }
            closedir(d);
            std::sort(app.video_paths.begin(), app.video_paths.end());
        }
        if (!app.video_paths.empty()) app.load_video_at(0);
        std::fprintf(stderr, "[vivid] %zu video clips found\n", app.video_paths.size());
    }

    vivid::ui::NodeGraph graph;
    graph.set_visual_graph(&vgraph);   // show the op-chain; generator node toggles Plasma/Video
    app.graph = &graph;
    vivid::ui::ClipEditor clip_editor;
    win.editor = &clip_editor;

    Transport transport;
    app.transport = &transport;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 0;  // device default
    cfg.dataCallback = audio_callback;
    cfg.pUserData = &app;   // the audio thread sees the shared App, never a Window

    ma_device device;
    bool audio_ok = (ma_device_init(nullptr, &cfg, &device) == MA_SUCCESS);
    if (audio_ok) {
        // Now that we know the device sample rate, scan + load an instrument.
        app.session = vivid_poc::session_create(device.sampleRate);
        std::fprintf(stderr, "[vivid] session: %d tracks (track 0: %s)\n",
                     app.session ? vivid_poc::session_track_count(app.session) : 0,
                     app.session ? vivid_poc::session_track_name(app.session, 0) : "none — test tone");
        if (ma_device_start(&device) != MA_SUCCESS) audio_ok = false;
    }
    glfwSetWindowUserPointer(window, &win);
    vivid::install_input_callbacks(window);  // key/char/scroll/mouse (app/input.cpp)
    std::fprintf(stderr, "[vivid] audio: %s (%u Hz)\n",
                 audio_ok ? "running" : "unavailable", audio_ok ? device.sampleRate : 0);

    // MCP control server: a loopback HTTP endpoint the agent bridge drives. Commands
    // are queued on the HTTP thread and applied on the main thread each frame.
    vivid::ControlServer control;
    app.control = &control;
    vivid::ControlCtx cctx{ app.session, &graph, &vgraph, &transport,
                            &win.win_w, &win.win_h, &win.split_x, &win.dock_h };
    { const char* pe = std::getenv("VIVID_PORT"); control.start(pe ? std::atoi(pe) : 9876); }

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

        vivid::FrameState frame;
        if (gpu.begin_frame(frame)) {
            const float tsec = static_cast<float>(glfwGetTime());
            // the generator (set by V or the generator node) drives the video source
            app.visual_source = (vgraph.generator() == vivid::VOp::Video) ? 1 : 0;
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
    vivid::macos_run_frame_loop(poll_events, tick);

    control.stop();   // stop the MCP control server thread before tearing down state
    if (audio_ok) ma_device_uninit(&device);  // stops the callback first
    for (int t = 0; t < 8; ++t) if (win.track_win[t]) vst3_plugin_window_close(win.track_win[t]);
    for (int k = 0; k < 8; ++k) if (win.fx_win[k]) vst3_plugin_window_close(win.fx_win[k]);
    if (app.session) vivid_poc::session_destroy(app.session);
    if (app.video) { video_close(app.video); app.video = nullptr; }
    vgraph.shutdown();
    srcTex.release();
    ui.shutdown();
    gpu.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
