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
#include "app/shell.h"
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
    vivid::install_input_callbacks(window);  // key/char/scroll/mouse (app/input.cpp)
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
