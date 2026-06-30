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
#include "app/frame.h"
#include "gpu/builtin_ops.h"
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
#include "operator_api/gpu_operator.h"   // P2.0 spike (TEMP): VividGpuContext + descriptor/ABI
#include <dlfcn.h>                        // P2.0 spike (TEMP): dlopen
#include <dirent.h>
#include <vector>
#include <string>
#include <algorithm>
#include "miniaudio.h"

namespace { using namespace vivid::ui; }  // kViewW/H + constants (ui/layout.h)

int main() {
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // WebGPU owns the surface
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Vivid PoC — foundation", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "glfwCreateWindow failed\n"); glfwTerminate(); return 1; }

    vivid::App app;        // shared engine/document (one per process)
    vivid::Window win;     // this window's view + interaction state
    win.app = &app;
    win.glfw = window;

    // Register the built-in visual operators + validate their descriptors (the
    // operator-based visuals model; VisualGraph drives them from P1.3). A loud
    // startup check that the real ops are well-formed (named codes).
    vivid::register_builtin_ops(app.op_registry);
    { int bad = 0;
      for (const auto& nm : app.op_registry.type_names()) {
          std::vector<vivid::DescriptorValidationIssue> iss;
          app.op_registry.create(nm, iss);
          for (const auto& i : iss) { std::fprintf(stderr, "[vivid] op '%s' descriptor: %s — %s\n", nm.c_str(), i.code.c_str(), i.message.c_str()); ++bad; }
      }
      std::fprintf(stderr, "[vivid] registered %zu visual ops%s\n",
                   app.op_registry.type_names().size(), bad ? " (WITH ISSUES)" : " (all valid)");
    }

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

    // ===================== P2.0 SPIKE (TEMPORARY) =========================
    // Prove the dlopen boundary: load a standalone operator .dylib, hand it the
    // HOST's WGPUDevice via a VividGpuContext, and render it into an offscreen
    // target. Success = loads + ABI matches + zero "[vivid] WebGPU error" lines.
    // Superseded by the P2.1 loader+scan; remove this block then.
    {
        void* h = dlopen("@executable_path/../PlugIns/vivid_spike_solid.dylib", RTLD_NOW | RTLD_LOCAL);
        if (!h) { std::fprintf(stderr, "[spike] dlopen FAILED: %s\n", dlerror()); }
        else {
            auto abi     = reinterpret_cast<uint32_t (*)()>(dlsym(h, "vivid_abi_version"));
            auto desc_fn = reinterpret_cast<const VividOperatorDescriptor* (*)()>(dlsym(h, "vivid_descriptor"));
            auto create  = reinterpret_cast<void* (*)()>(dlsym(h, "vivid_create"));
            auto destroy = reinterpret_cast<void (*)(void*)>(dlsym(h, "vivid_destroy"));
            auto proc    = reinterpret_cast<void (*)(void*, VividGpuContext*)>(dlsym(h, "vivid_process_gpu"));
            std::fprintf(stderr, "[spike] dlopen ok: abi=%u (host=%u) name=%s create=%d proc=%d\n",
                         abi ? abi() : 0u, VIVID_OPERATOR_ABI_VERSION,
                         desc_fn ? desc_fn()->name : "?", create != nullptr, proc != nullptr);
            if (abi && abi() == VIVID_OPERATOR_ABI_VERSION && create && proc && destroy) {
                void* inst = create();
                WGPUTextureDescriptor td{};
                td.usage = WGPUTextureUsage_RenderAttachment;
                td.dimension = WGPUTextureDimension_2D;
                td.size = { 64, 64, 1 }; td.format = gpu.surface_format();
                td.mipLevelCount = 1; td.sampleCount = 1;
                WGPUTexture tex = wgpuDeviceCreateTexture(gpu.device(), &td);
                WGPUTextureViewDescriptor vd{}; vd.format = gpu.surface_format();
                vd.dimension = WGPUTextureViewDimension_2D; vd.mipLevelCount = 1;
                vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
                WGPUTextureView view = wgpuTextureCreateView(tex, &vd);
                WGPUCommandEncoderDescriptor ed{};
                WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(gpu.device(), &ed);
                float pv[1] = { 0.5f };
                VividGpuContext ctx{};
                ctx.device = gpu.device(); ctx.queue = gpu.queue(); ctx.command_encoder = enc;
                ctx.output_texture = tex; ctx.output_texture_view = view;
                ctx.output_width = 64; ctx.output_height = 64; ctx.output_format = gpu.surface_format();
                ctx.param_values = pv; ctx.time = 0.0;
                proc(inst, &ctx);   // dylib issues wgpu calls on the host device
                WGPUCommandBufferDescriptor cd{};
                WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cd);
                wgpuQueueSubmit(gpu.queue(), 1, &cb);
                std::fprintf(stderr, "[spike] render submitted via host device — watch for WebGPU errors above/below\n");
                wgpuCommandBufferRelease(cb); wgpuCommandEncoderRelease(enc);
                wgpuTextureViewRelease(view); wgpuTextureRelease(tex);
                destroy(inst);
            }
        }
    }
    // =================== end P2.0 SPIKE (TEMPORARY) =======================

    vivid::ui::Renderer2D ui;
    if (!ui.init(gpu.device(), gpu.surface_format(), VIVID_FONT_PATH, 15.0f, win.dpi))
        std::fprintf(stderr, "[vivid] Renderer2D init failed (UI disabled)\n");
    win.ui = &ui;

    // Composable visuals chain (generator -> feedback -> blur -> viewer).
    const uint32_t kRtW = static_cast<uint32_t>(kViewW), kRtH = static_cast<uint32_t>(kViewH);
    vivid::VisualGraph vgraph;
    if (!vgraph.init(gpu.device(), gpu.queue(), gpu.surface_format(), kRtW, kRtH, &app.op_registry))
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
    { const char* pe = std::getenv("VIVID_PORT"); control.start(pe ? std::atoi(pe) : 9876); }

    vivid::run_frame_loop(app, win);   // blocks until the window closes (app/frame.cpp)

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
