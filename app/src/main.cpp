// Vivid — entry point. Constructs the shared engine (App) + a single view
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
#include <cstdlib>
#include <cstring>

#include "gpu/gpu_context.h"
#include "gpu/gpu_util.h"
#include "transport.h"
#include "audio/vst3_host.h"
#include "ui/renderer_2d.h"
#include "ui/node_graph.h"
#include "ui/ui_style.h"
#include "ui/layout.h"
#include "app/app.h"
#include "app/edit_gateway.h"   // ADR-0017 undo/redo command sink
#include "app/window.h"
#include "app/input.h"
#include "app/frame.h"
#include "app/file_actions.h"      // File-menu actions (native menu bar)
#include "app/window_prefs.h"       // launch sizing + remembered window size/pos
#include "platform/menu_bar.h"     // install_menu_bar
#include "audio/builtin_audio_ops.h"   // AO-1: native audio operators
#include "audio/plugin_scan.h"         // background plugin classifier (instrument vs effect)
#include "audio/plugin_probe.h"        // --probe-plugin subprocess entry point
#include "audio/audio_callback.h"
#include "ui/mapping_overview.h"
#include "ui/session_view.h"
#include "cli/control_server.h"
#include "ui/clip_editor.h"
#include "persist.h"
#include "gpu/shader_op.h"
#include "audio/vst3_plugin_window.h"
#include "platform/app_nap.h"
#include "gpu/effect_op.h"
#include "gpu/render_target.h"
#include "gpu/visual_graph.h"
#include "gpu/texture_source.h"
#include "gpu/video_player.h"
#include "gpu/splash.h"          // animated startup splash (nebula + node-graph "V")
#include "gpu/gpu_util.h"        // kMsaaSamples (splash pipeline must match the frame)
#include "gpu/operator_scan.h"   // P2.1: load operator dylibs at startup
#include <functional>
#include "packages/package_manager.h"  // P2.3: the managed installed-operators dir
#include "platform/platform.h"   // P3: executable_path (locate the bundle PlugIns/)
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include "miniaudio.h"

namespace { using namespace vivid::ui; }  // layout constants (ui/layout.h)

int main(int argc, char** argv) {
    // Probe subprocess: `vivid --probe-plugin <bundle> --format <n>` opens ONE plugin, prints what
    // it is as JSON, and exits. The app re-execs itself this way (audio/plugin_scan.cpp) so a
    // plugin that segfaults or hangs while being opened kills a throwaway child instead of Vivid.
    // Must run before ANY window/audio/GPU init — this process is not a UI.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--probe-plugin") != 0 || i + 1 >= argc) continue;
        const std::string path = argv[i + 1];
        int format = 0;
        for (int j = 1; j + 1 < argc; ++j)
            if (std::strcmp(argv[j], "--format") == 0) format = std::atoi(argv[j + 1]);
        return vivid::session::run_probe_subprocess_main(path, format);
    }

    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // WebGPU owns the surface
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Vivid", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "glfwCreateWindow failed\n"); glfwTerminate(); return 1; }

    // Size the window to a fraction of the current monitor (capped, centered) on first
    // launch; restore the remembered size/position after that (see app/window_prefs.h).
    {
        int waX = 0, waY = 0, waW = 0, waH = 0;
        if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) glfwGetMonitorWorkarea(mon, &waX, &waY, &waW, &waH);
        const vivid::WindowPrefs wp = vivid::load_window_prefs(vivid::window_prefs_path());
        const vivid::LaunchRect lr = vivid::compute_launch_rect(
            waX, waY, waW, waH, wp, vivid::kLaunchMaxW, vivid::kLaunchMaxH, vivid::kLaunchFraction);
        glfwSetWindowSize(window, lr.w, lr.h);
        glfwSetWindowPos(window, lr.x, lr.y);
        std::fprintf(stderr, "[vivid] window: %dx%d at (%d,%d) [workarea %dx%d]%s\n",
                     lr.w, lr.h, lr.x, lr.y, waW, waH, wp.has_size ? " (restored)" : " (default %)");
    }

    vivid::App app;        // shared engine/document (one per process)
    vivid::Window win;     // this window's view + interaction state
    win.app = &app;
    win.glfw = window;

    // Keep the frame loop (and the control-server drain it runs each tick) pumping
    // even when the app is backgrounded, so an agent can drive it over MCP without
    // the window being frontmost.
    vivid::disable_app_nap("Vivid control server / agent-driven rendering");

    // Register the native audio operators (visual operators are all auto-discovered
    // package dylibs — see the PlugIns/ scan just below).
    vivid::register_builtin_audio_ops(app.op_registry);   // AO-1: native audio operators
    // P2.1: also load operator dylibs dropped in the bundle PlugIns/ (or the dev
    // override $VIVID_OPERATORS_DIR). Loaded ops register by descriptor name and
    // flow through OpRegistry identically to built-ins (built-ins win on a clash).
    { namespace fs = std::filesystem;
      std::vector<std::string> dirs;
      dirs.push_back(vivid::user_operators_dir());   // installed packages (or $VIVID_OPERATORS_DIR)
      const std::string exe = vivid::platform::executable_path();
      if (!exe.empty()) {
          const fs::path exe_dir = fs::path(exe).parent_path();
          dirs.push_back((exe_dir / ".." / "PlugIns").lexically_normal().string());  // macOS .app bundle
          dirs.push_back((exe_dir / "PlugIns").lexically_normal().string());          // non-bundle (Linux/Windows)
      }
      int loaded = 0;
      for (const auto& d : dirs) loaded += vivid::scan_operator_dir(d, app.op_registry, app.op_loaders);

      // ADR-0016: a shader FILE is an operator. Each .wgsl/.glsl in the library declares its
      // own params in a JSON header and registers as its own type — so shaders reach the Tab
      // chooser, list_operators, mappings and persistence through exactly the same door as a
      // compiled op, with no separate machinery.
      const int shaders = app.shader_library.scan(app.op_registry);
      int shader_errors = 0;
      for (const auto& e : app.shader_library.entries()) if (!e.error.empty()) ++shader_errors;
      std::fprintf(stderr, "[vivid] %d shader ops (%zu files scanned%s)\n", shaders,
                   app.shader_library.entries().size(),
                   shader_errors ? ", SOME WITH ERRORS" : "");

      // Validate every op (built-in + loaded) loudly at startup (named codes).
      int bad = 0;
      for (const auto& nm : app.op_registry.type_names()) {
          std::vector<vivid::DescriptorValidationIssue> iss;
          app.op_registry.create(nm, iss);
          for (const auto& i : iss) { std::fprintf(stderr, "[vivid] op '%s' descriptor: %s — %s\n", nm.c_str(), i.code.c_str(), i.message.c_str()); ++bad; }
      }
      std::fprintf(stderr, "[vivid] %zu visual ops (%d loaded from disk)%s\n",
                   app.op_registry.type_names().size(), loaded, bad ? " (WITH ISSUES)" : " (all valid)");
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

    vivid::ui::Renderer2D ui;
    if (!ui.init(gpu.device(), gpu.surface_format(), VIVID_FONT_PATH, 15.0f, win.dpi))
        std::fprintf(stderr, "[vivid] Renderer2D init failed (UI disabled)\n");
    win.ui = &ui;

    // Animated startup splash: a branded frame shown while the engine finishes booting
    // (the blocking bit is the default project's VST3 instrument load in session_create,
    // which drives it via the load-progress callback below).
    vivid::Splash splash;
    splash.init(gpu.device(), gpu.surface_format(), vivid::kMsaaSamples);
    std::function<void(const char*)> render_splash =
        [&](const char* status) { splash.render(gpu, ui, window, status); };
    // Force the window on-screen now so the splash is visible during the blocking load
    // (macOS otherwise defers first composite until the run loop turns / the app activates).
    glfwShowWindow(window);
    glfwFocusWindow(window);
    for (int i = 0; i < 4; ++i) glfwPollEvents();
    render_splash("Starting up...");

    // Composable visuals chain (generator -> feedback -> blur -> viewer). The render targets open
    // at the Output node's default format (1280x720); the active Output node's params own the size
    // from the first frame on (ADR-0014), so this is only the pre-first-frame value.
    uint32_t kRtW = 0, kRtH = 0;
    vivid::output_size_for(vivid::kDefaultAspect, vivid::kDefaultHeight, kRtW, kRtH);
    vivid::VisualGraph vgraph;
    if (!vgraph.init(gpu.device(), gpu.queue(), gpu.surface_format(), kRtW, kRtH, &app.op_registry))
        std::fprintf(stderr, "[vivid] visual graph init failed (viewer disabled)\n");
    app.vgraph = &vgraph;

    // Hot-reload (opt-in/dev): point VIVID_WATCH_PACKAGE at a package dir to install
    // it, register its operators, and watch their sources — editing a source live-
    // recompiles + hot-swaps the dylib (params preserved). Drives the frame loop's tick.
    app.hot_reload.start(&app.op_registry, &vgraph);
    if (const char* wp = std::getenv("VIVID_WATCH_PACKAGE")) {
        namespace fs = std::filesystem;
        const std::string pkg = wp;
        vivid::PackageInstallResult ir = vivid::install_package(pkg);
        vivid::PackageManifest mf = vivid::parse_package_manifest(pkg);
        if (!ir.ok) std::fprintf(stderr, "[vivid] VIVID_WATCH_PACKAGE install failed: %s\n", ir.error.c_str());
        for (size_t i = 0; i < ir.compiles.size() && i < mf.operators.size(); ++i) {
            const auto& cr = ir.compiles[i];
            if (!cr.success) { std::fprintf(stderr, "[vivid] watch compile failed (%s):\n%s\n",
                                            mf.operators[i].name.c_str(), cr.error_output.c_str()); continue; }
            // Ensure it's registered (no-op if the startup scan already loaded it),
            // then find its live loader by descriptor name (the registry key) to watch.
            vivid::load_and_register_operator(cr.dylib_path, app.op_registry, app.op_loaders);
            const std::string& opname = mf.operators[i].name;
            vivid::OperatorLoader* loader = nullptr;
            for (auto& l : app.op_loaders)
                if (l->descriptor() && opname == l->descriptor()->name) { loader = l.get(); break; }
            if (loader)
                app.hot_reload.watch_op(opname, pkg, mf.operators[i],
                                        (fs::path(pkg) / mf.operators[i].source).string(), loader);
        }
    }

    // Texture source (image/video) — seeded with a test pattern; P19b feeds video.
    vivid::TextureSource srcTex;
    srcTex.init(gpu.device(), 512, 288, gpu.surface_format());
    { auto pat = vivid::gen_test_pattern(512, 288); srcTex.upload(gpu.queue(), pat.data()); }
    app.srcTex = &srcTex;

    // Optional project media root (N cycles, V shows video). No hardcoded local paths:
    // projects/MCP can set this explicitly, and missing roots are reported in health/status.
    if (const char* root = std::getenv("VIVID_MEDIA_ROOT")) app.set_media_root(root);

    vivid::ui::NodeGraph graph;
    graph.set_visual_graph(&vgraph);   // show the op-chain; generator node toggles Plasma/Video
    graph.set_shader_library(&app.shader_library);   // ADR-0016: badge shader rows in the Tab chooser
    app.graph = &graph;
    vivid::ui::ClipEditor clip_editor;
    win.editor = &clip_editor;
    clip_editor.set_audition_cb([&app](int track, int pitch, float vel, bool on) {   // keyboard audition
        if (!app.session) return;
        if (on) vivid::session::session_preview_note(app.session, track, pitch, vel);
        else    vivid::session::session_preview_off(app.session, track, pitch);
    });

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
        // Now that we know the device sample rate, scan + load the default project. This
        // blocks for seconds on VST3 instrument load — drive the splash from each phase.
        vivid::session::session_set_load_progress(
            [](void* u, const char* s) { (*static_cast<std::function<void(const char*)>*>(u))(s); },
            &render_splash);
        app.session = vivid::session::session_create(device.sampleRate);
        vivid::session::session_set_load_progress(nullptr, nullptr);
        vivid::session::session_set_op_registry(app.session, &app.op_registry);   // AO-1: native audio ops
        // Classify the installed plugins (instrument vs effect) in the background: cached verdicts
        // apply instantly, only new/changed plugins are probed. Kicked explicitly HERE and not from
        // the catalog's lazy first query — that one happens inside a draw call.
        vivid::session::plugin_scan_start();
        vivid::session::session_build_split_showcase(app.session);   // node-graph demo tracks (needs the registry)
        std::fprintf(stderr, "[vivid] session: %d tracks (track 0: %s)\n",
                     app.session ? vivid::session::session_track_count(app.session) : 0,
                     app.session ? vivid::session::session_track_name(app.session, 0) : "none — test tone");
        if (ma_device_start(&device) != MA_SUCCESS) audio_ok = false;
    }
    glfwSetWindowUserPointer(window, &win);
    vivid::install_input_callbacks(window);  // key/char/scroll/mouse (app/input.cpp)

    // Native macOS File menu (New/Open/Save/Save As + Open Recent). Actions run on the
    // main thread, so they touch the session/graph directly (app/file_actions.cpp).
    {
        vivid::platform::MenuActions ma;
        ma.new_project     = [&] { vivid::file_actions::new_project(app); };
        ma.open_project    = [&] { vivid::file_actions::open(window, win, app); };
        ma.save_project    = [&] { vivid::file_actions::save(window, win, app); };
        ma.save_project_as = [&] { vivid::file_actions::save_as(window, win, app); };
        ma.open_recent     = [&](const std::string& p) { vivid::file_actions::open_recent(window, win, app, p); };
        vivid::platform::install_menu_bar(ma);
        vivid::platform::set_recent_projects(app.project.recent_project_paths);
    }
    std::fprintf(stderr, "[vivid] audio: %s (%u Hz)\n",
                 audio_ok ? "running" : "unavailable", audio_ok ? device.sampleRate : 0);

    // MCP control server: a loopback HTTP endpoint the agent bridge drives. Commands
    // are queued on the HTTP thread and applied on the main thread each frame.
    vivid::ControlServer control;
    app.control = &control;
    { const char* pe = std::getenv("VIVID_PORT"); control.start(pe ? std::atoi(pe) : 9876); }

    if (app.midi_in.start())   // hardware MIDI input -> armed track (M6.4)
        std::fprintf(stderr, "[vivid] MIDI input: %d source(s) connected\n", app.midi_in.source_count());

    // ADR-0017 undo/redo: the edit gateway (a local, like `control` above). The baseline (undo
    // entry 0) is seeded from inside the frame loop, at the end of the FIRST tick — after the graph
    // has laid out its nodes, so entry 0 holds the settled document (a pre-layout baseline would make
    // the first undo jerk every node to its pre-layout position).
    vivid::EditGateway gateway(app);
    app.edit_gateway = &gateway;

    vivid::run_frame_loop(app, win);   // blocks until the window closes (app/frame.cpp)

    // Remember this window's size + position for next launch (app-level, not per-project).
    { int w = 0, h = 0, x = 0, y = 0;
      glfwGetWindowSize(window, &w, &h); glfwGetWindowPos(window, &x, &y);
      if (w > 0 && h > 0) vivid::save_window_prefs({ w, h, x, y, true, true }, vivid::window_prefs_path()); }

    app.midi_in.stop();   // stop hardware MIDI before tearing down state
    control.stop();   // stop the MCP control server thread before tearing down state
    if (audio_ok) ma_device_uninit(&device);  // stops the callback first
    for (int t = 0; t < 8; ++t) if (win.track_win[t]) vst3_plugin_window_close(win.track_win[t]);
    for (int k = 0; k < 8; ++k) if (win.fx_win[k]) vst3_plugin_window_close(win.fx_win[k]);
    vivid::session::plugin_scan_stop();   // join the classifier worker before anything it touches dies
    if (app.session) vivid::session::session_destroy(app.session);
    if (app.video) { video_close(app.video); app.video = nullptr; }
    vgraph.shutdown();
    srcTex.release();
    ui.shutdown();
    gpu.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
