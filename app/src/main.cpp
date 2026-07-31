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
#include "ui/audio_node_graph.h"
#include "ui/ui_style.h"
#include "ui/layout.h"
#include "app/app.h"
#include "app/edit_gateway.h"   // ADR-0017 undo/redo command sink
#include "app/window.h"
#include "app/input.h"
#include "app/frame.h"
#include "app/file_actions.h"      // File-menu actions (native menu bar)
#include "app/autosave.h"          // ADR-0018 autosave recovery on launch
#include "app/crash_guard.h"       // ADR-0018 install_crash_handlers
#include "app/crash_recovery.h"    // ADR-0018 CrashRecovery (record + warm snapshot)
#include "app/quarantine.h"        // ADR-0018 R3 quarantine (skip repeat crashers)
#include <optional>
#include <set>
#include "platform/file_dialog.h"  // ADR-0018 confirm_discard_changes (New/Open save-confirm)
#include "app/examples.h"          // bundled example projects (ADR-0021/P2)
#include "app/window_prefs.h"       // launch sizing + remembered window size/pos
#include "app/video_recorder.h"     // realtime AV video export (File > Export Video / MCP)
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
#include "audio/clap_plugin_window.h"
#include "platform/app_nap.h"
#include "gpu/effect_op.h"
#include "gpu/render_target.h"
#include "gpu/visual_graph.h"
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

    // ADR-0018 (R1/R2): install fatal-signal handlers FIRST, then reconstruct any prior crash from
    // its marker + warm snapshot and point the handler at our marker/snapshot files. A crash inside
    // an operator's process_*() is now named (crash_guard.h), recorded to {config}/crashes/, and
    // surfaced at startup. `recovery` outlives the frame loop, which writes warm snapshots through it.
    vivid::install_crash_handlers();
    vivid::CrashRecovery recovery((std::filesystem::path(vivid::platform::user_data_dir()) / "crashes").string());
    std::optional<vivid::CrashRecord> prior_crash = recovery.init();
    recovery.install_signal_paths();
    app.crash_recovery = &recovery;
    if (prior_crash) {   // surface it — logged before frame 1, so ADR-0019's bridge auto-toasts it
        std::string where = prior_crash->operator_name.empty() ? std::string("(unknown)") : prior_crash->operator_name;
        if (!prior_crash->node_id.empty()) where += " (node " + prior_crash->node_id + ")";
        VLOG_ERR(app, "Previous run crashed: %s in operator %s", prior_crash->signal_name.c_str(), where.c_str());
    }

    // ADR-0018 (R3): a repeat crasher (≥3 in 24h) is DISABLED by default this launch — recomputed
    // statelessly from the crash history. `--safe-mode` additionally disables the last crasher (so a
    // single bad op that isn't yet quarantined can't brick the session). Disabled ops are skipped at
    // op-load below (their persisted nodes then load as op_missing(), ADR-0019); the set is stored on
    // App so the Tab chooser can grey them with a reason.
    bool safe_mode = false;
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "--safe-mode") == 0) safe_mode = true;
    std::set<std::string> disabled_ops = vivid::quarantined_types(recovery.crash_dir());
    for (const auto& q : vivid::scan_quarantine(recovery.crash_dir()))
        VLOG_WARN(app, "operator '%s' quarantined: crashed %d\xC3\x97 in 24h — clear its crash history to re-enable",
                  q.type_name.c_str(), q.crash_count);
    if (safe_mode && prior_crash && !prior_crash->operator_name.empty()) {
        disabled_ops.insert(prior_crash->operator_name);
        VLOG_WARN(app, "safe mode: operator '%s' (last crash) disabled this launch", prior_crash->operator_name.c_str());
    }
    app.quarantined_ops = disabled_ops;

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
      for (const auto& d : dirs) loaded += vivid::scan_operator_dir(d, app.op_registry, app.op_loaders, &disabled_ops);
      app.file_drops.rebuild(app.op_loaders);   // ADR-0021/P3: index drop handlers of the loaded ops

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

    // ADR-0020: the operator hot-reload watcher is ON BY DEFAULT (like the shader watcher). At
    // startup we discover package sources in the standard user locations — anything the user cloned
    // to edit ("Clone & Edit" scaffolds into ~/.../clones) or installed — and watch each already-
    // loaded operator, so editing its source live-recompiles + hot-swaps the dylib (params
    // preserved). No env var required; on an installed app with no local package sources this
    // simply finds nothing to watch. $VIVID_WATCH_PACKAGE remains an override for a source tree
    // outside the standard locations (below). Drives the frame loop's tick.
    app.hot_reload.start(&app.op_registry, &vgraph, &app.log);
    { namespace fs = std::filesystem;
      const std::string clones = (fs::path(vivid::platform::user_data_dir()) / "clones").string();
      std::vector<vivid::PackageManifest> pkg_errors;   // Ph5 P2-02: don't drop a malformed package silently
      for (const auto& mf : vivid::discover_packages(clones, &pkg_errors))                      app.hot_reload.watch_manifest(app.op_loaders, mf);
      for (const auto& mf : vivid::discover_packages(vivid::user_operators_dir(), &pkg_errors)) app.hot_reload.watch_manifest(app.op_loaders, mf);
      for (const auto& e : pkg_errors)   // ADR-0019: a bad operator package is surfaced, not silently ignored
          VLOG_ERR(app, "operator package '%s' ignored: %s", e.dir.c_str(), e.error.c_str());
    }
    if (const char* wp = std::getenv("VIVID_WATCH_PACKAGE")) {
        const std::string pkg = wp;
        vivid::PackageInstallResult ir = vivid::install_package(pkg);
        if (!ir.ok) std::fprintf(stderr, "[vivid] VIVID_WATCH_PACKAGE install failed: %s\n", ir.error.c_str());
        for (const auto& cr : ir.compiles)   // register the freshly-compiled dylibs so watch can find their loaders
            if (cr.success) vivid::load_and_register_operator(cr.dylib_path, app.op_registry, app.op_loaders);
        app.hot_reload.watch_manifest(app.op_loaders, vivid::parse_package_manifest(pkg));
    }

    // Optional project media root (the base a Video/Image node's relative path resolves against).
    // No hardcoded local paths: projects/MCP set this explicitly, and missing roots are reported in
    // health/status. Video is decoded per-node now (the self-decoding Video op owns its own file).
    if (const char* root = std::getenv("VIVID_MEDIA_ROOT")) app.set_media_root(root);

    vivid::ui::NodeGraph graph;
    graph.set_visual_graph(&vgraph);   // show the op-chain; generator node toggles Plasma/Video
    graph.set_shader_library(&app.shader_library);   // ADR-0016: badge shader rows in the Tab chooser
    graph.set_quarantined(app.quarantined_ops);       // ADR-0018: grey quarantined ops in the chooser
    app.graph = &graph;
    vivid::ui::AudioNodeGraph audio_graph;   // ADR-0023 step 6: one persistent audio-graph view (re-primed per use)
    app.audio_graph = &audio_graph;
    vivid::ui::ClipEditor clip_editor;
    win.editor = &clip_editor;
    clip_editor.set_audition_cb([&app](int track, int pitch, float vel, bool on) {   // keyboard audition
        if (!app.session) return;
        if (on) vivid::session::session_preview_note(app.session, track, pitch, vel);
        else    vivid::session::session_preview_off(app.session, track, pitch);
    });

    Transport transport;
    app.transport = &transport;

    vivid::VideoRecorder video_recorder;   // realtime AV export; driven per-frame in run_frame_loop
    app.recorder = &video_recorder;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 0;  // device default
    cfg.dataCallback = audio_callback;
    cfg.pUserData = &app;   // the audio thread sees the shared App, never a Window
    // Give the RT callback real headroom. The callback hosts several plugin synths (Surge/CLAP + VST3)
    // whose per-block cost SPIKES on dense passages (e.g. a 16th-note arp stacking voices) even though
    // average CPU is low — with miniaudio's tiny low-latency default period those spikes overrun a single
    // callback's deadline and crackle. A ~23 ms period (1024 frames @ 44.1k) absorbs the spikes; the extra
    // latency is imperceptible for playback and audio-reactive visuals. Bump higher if a heavier session
    // still crackles.
    cfg.periodSizeInFrames = 1024;
    cfg.performanceProfile = ma_performance_profile_conservative;

    ma_device device;
    bool audio_ok = (ma_device_init(nullptr, &cfg, &device) == MA_SUCCESS);
    if (audio_ok) {
        transport.configure_capture(device.sampleRate, 30.0);
        // Now that we know the device sample rate, create the (empty) session engine. The app
        // starts clean — no baked-in content; a project is loaded via File > Open. The splash
        // progress hook stays wired for any future load phases.
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
        std::fprintf(stderr, "[vivid] session: %d tracks (empty — clean start)\n",
                     app.session ? vivid::session::session_track_count(app.session) : 0);
        if (ma_device_start(&device) != MA_SUCCESS) audio_ok = false;
    }
    glfwSetWindowUserPointer(window, &win);
    vivid::install_input_callbacks(window);  // key/char/scroll/mouse (app/input.cpp)

    // Native macOS File menu (New/Open/Save/Save As + Open Recent). Actions run on the
    // main thread, so they touch the session/graph directly (app/file_actions.cpp).
    {
        vivid::platform::MenuActions ma;
        // ADR-0018: before an action that discards the current document (New/Open), if there are
        // unsaved changes, confirm. Save → save (aborts if the save dialog is cancelled); Cancel →
        // abort the action. MCP load/new bypass this (no user at the machine to answer a modal).
        auto ok_to_discard = [&]() -> bool {
            if (!app.edit_gateway || !app.edit_gateway->dirty()) return true;
            switch (vivid::platform::confirm_discard_changes()) {
                case vivid::platform::DiscardChoice::Save:
                    vivid::file_actions::save(window, win, app);
                    return !app.edit_gateway->dirty();   // still dirty ⇒ the save dialog was cancelled ⇒ abort
                case vivid::platform::DiscardChoice::Discard: return true;
                case vivid::platform::DiscardChoice::Cancel:  return false;
            }
            return true;
        };
        ma.new_project     = [&] { if (ok_to_discard()) vivid::file_actions::new_project(app); };
        ma.open_project    = [&] { if (ok_to_discard()) vivid::file_actions::open(window, win, app); };
        ma.save_project    = [&] { vivid::file_actions::save(window, win, app); };
        ma.save_project_as = [&] { vivid::file_actions::save_as(window, win, app); };
        ma.open_recent     = [&](const std::string& p) { if (ok_to_discard()) vivid::file_actions::open_recent(window, win, app, p); };
        ma.open_example    = [&](const std::string& p) { if (ok_to_discard()) vivid::file_actions::open_recent(window, win, app, p); };
        // ADR-0017/G4: Edit > Undo/Redo. app.edit_gateway is created below (read at click time).
        ma.undo            = [&] { if (app.edit_gateway) app.edit_gateway->undo(); };
        ma.redo            = [&] { if (app.edit_gateway) app.edit_gateway->redo(); };
        // ADR-0026: the Eval menu. "Set Gemini Key…" opens the in-app modal (input.cpp owns the
        // keyboard while it's up). "Evaluate Output" captures 20s of the live master and kicks off an
        // async Gemini eval; the frame loop toasts the verdict when the job lands. Fail-closed here too.
        ma.set_gemini_key  = [&] { win.show_gemini_key = true; win.gemini_key_buf.clear(); };
        ma.evaluate_output = [&] {
            if (!app.music_eval.has_key()) {
                vivid::ui::push_toast(win.toasts, vivid::LogLevel::Warning,
                    "No Gemini key — Eval \xE2\x96\xB8 Set Gemini Key\xE2\x80\xA6", glfwGetTime());
                return;
            }
            std::vector<float> L, R; uint32_t sr = 0;
            if (!app.transport || app.transport->capture_snapshot(20.0, L, R, &sr) == 0 || sr == 0) {
                vivid::ui::push_toast(win.toasts, vivid::LogLevel::Warning,
                    "Nothing playing to evaluate", glfwGetTime());
                return;
            }
            win.music_eval_job = app.music_eval.start_eval(vivid::pcm16_wav_from_planar(L, R, sr), "caption");
            vivid::ui::push_toast(win.toasts, vivid::LogLevel::Info, "Evaluating output\xE2\x80\xA6", glfwGetTime());
        };
        // File > Export Video: toggle a realtime AV export. Start → save dialog → record the live
        // Output + master audio; the menu item becomes "Stop Export". Stop → finalize + toast.
        ma.export_video = [&] {
            if (!app.recorder) return;
            if (app.recorder->is_recording()) {
                const auto st = app.recorder->stop();
                char m[192];
                std::snprintf(m, sizeof m, "Video exported: %s (%llu frames, %.1fs)",
                              st.path.c_str(), static_cast<unsigned long long>(st.frames), st.elapsed_sec);
                vivid::ui::push_toast(win.toasts, vivid::LogLevel::Info, m, glfwGetTime(), 10.0);
                return;
            }
            const std::string path = vivid::platform::save_video_dialog("vivid-export.mp4");
            if (path.empty()) return;   // cancelled
            const uint32_t sr = audio_ok ? static_cast<uint32_t>(device.sampleRate) : 0;
            std::string err;
            if (app.vgraph && app.transport &&
                app.recorder->start(path, 60.0, 0.0, app.vgraph->rt_w(), app.vgraph->rt_h(),
                                    sr, *app.transport, &err)) {
                vivid::ui::push_toast(win.toasts, vivid::LogLevel::Info,
                    "Recording video\xE2\x80\xA6  (File \xE2\x96\xB8 Stop Export)", glfwGetTime());
            } else {
                vivid::ui::push_toast(win.toasts, vivid::LogLevel::Warning,
                    "Export failed: " + err, glfwGetTime(), 8.0);
            }
        };
        vivid::platform::install_menu_bar(ma);
        vivid::platform::set_recent_projects(app.project.recent_project_paths);
        // File > Open Example — the bundled demos (ADR-0021/P2). Discovered once at startup.
        std::vector<vivid::platform::MenuItemEntry> examples;
        for (const auto& e : vivid::examples::discover_examples()) examples.push_back({ e.name, e.path });
        vivid::platform::set_example_projects(examples);
    }
    std::fprintf(stderr, "[vivid] audio: %s (%u Hz)\n",
                 audio_ok ? "running" : "unavailable", audio_ok ? device.sampleRate : 0);

    // MCP control server: a loopback HTTP endpoint the agent bridge drives. Commands
    // are queued on the HTTP thread and applied on the main thread each frame.
    vivid::ControlServer control;
    app.control = &control;
    // Wake the main loop the instant a request is queued, so it drains even when a backgrounded
    // app's CFRunLoop is App-Nap-throttled (glfwPostEmptyEvent is thread-safe, for exactly this).
    control.set_wake([]{ glfwPostEmptyEvent(); });
    { const char* pe = std::getenv("VIVID_PORT"); control.start(pe ? std::atoi(pe) : 9876); }

    if (app.midi_in.start())   // hardware MIDI input -> armed track (M6.4)
        std::fprintf(stderr, "[vivid] MIDI input: %d source(s) connected\n", app.midi_in.source_count());

    // ADR-0017 undo/redo: the edit gateway (a local, like `control` above). The baseline (undo
    // entry 0) is seeded from inside the frame loop, at the end of the FIRST tick — after the graph
    // has laid out its nodes, so entry 0 holds the settled document (a pre-layout baseline would make
    // the first undo jerk every node to its pre-layout position).
    vivid::EditGateway gateway(app);
    app.edit_gateway = &gateway;
    graph.set_edit_gateway(&gateway);   // ADR-0017/G2: capture UI graph edits

    // ADR-0018 (R4): offer to recover autosaved unsaved work left by a prior crash / kill. On accept,
    // load the autosave session, re-point the project so Save targets it, and mark it dirty once the
    // undo baseline seeds (app.recovered_unsaved, consumed in the frame loop) so quit/save prompt.
    {
        vivid::autosave::Recovery rec = vivid::autosave::check();
        // Escape hatches for headless / automated launches: the recovery MODAL blocks the frame loop
        // (and the control server it pumps) until dismissed, so a leftover autosave slot hangs an
        // unattended launch. VIVID_NO_RECOVER skips the prompt but leaves the slot intact for a later
        // interactive recovery. VIVID_DISCARD_RECOVERY is stronger: it explicitly discards the slot,
        // useful for disposable tutorial/test launches that must start clean.
        if (rec.available && std::getenv("VIVID_DISCARD_RECOVERY")) {
            vivid::autosave::clear();
            VLOG_WARN(app, "VIVID_DISCARD_RECOVERY set — discarded autosave recovery state");
            rec.available = false;
        } else if (rec.available && std::getenv("VIVID_NO_RECOVER")) {
            VLOG_WARN(app, "VIVID_NO_RECOVER set — skipping the autosave-recovery prompt");
            rec.available = false;
        }
        if (rec.available) {
            const std::string what = rec.project_path.empty() ? "an untitled project" : rec.project_path;
            if (vivid::platform::confirm_recover_autosave("Vivid found unsaved changes to " + what + ".")) {
                int ww = win.win_w, wh = win.win_h; float sx = win.split_x, dh = win.dock_h;
                float aox = 0.f, aoy = 0.f, ascale = 0.f;   // scale 0 = sentinel: no camera in the file
                if (vivid::load_session(rec.session_path, app.session, graph, ww, wh, sx, dh, aox, aoy, ascale)) {
                    win.split_x = sx; win.dock_h = dh;
                    if (ascale > 0.f) audio_graph.set_view({ aox, aoy, ascale });   // restore the camera (ADR-0023)
                    if (!rec.project_path.empty()) app.remember_project_path(rec.project_path);
                    app.recovered_unsaved = true;
                    VLOG_WARN(app, "recovered unsaved changes (%s)", what.c_str());
                } else VLOG_ERR(app, "autosave recovery failed to load");
            } else {
                vivid::autosave::clear();   // user discarded — drop the slot
            }
        }
    }

    vivid::run_frame_loop(app, win);   // blocks until the window closes (app/frame.cpp)

    // Finalize an in-flight video export BEFORE tearing down audio/GPU — otherwise the writer's
    // moov atom is never written and the file is corrupt. stop() is a no-op if not recording.
    if (app.recorder && app.recorder->is_recording()) app.recorder->stop();

    // Remember this window's size + position for next launch (app-level, not per-project).
    { int w = 0, h = 0, x = 0, y = 0;
      glfwGetWindowSize(window, &w, &h); glfwGetWindowPos(window, &x, &y);
      if (w > 0 && h > 0) vivid::save_window_prefs({ w, h, x, y, true, true }, vivid::window_prefs_path()); }

    app.midi_in.stop();   // stop hardware MIDI before tearing down state
    control.stop();   // stop the MCP control server thread before tearing down state
    if (audio_ok) ma_device_uninit(&device);  // stops the callback first
    for (int t = 0; t < 8; ++t) if (win.track_win[t]) vst3_plugin_window_close(win.track_win[t]);
    for (int k = 0; k < 8; ++k) if (win.fx_win[k]) vst3_plugin_window_close(win.fx_win[k]);
    for (int k = 0; k < 8; ++k) if (win.clap_win[k]) clap_plugin_window_close(win.clap_win[k]);
    vivid::session::plugin_scan_stop();   // join the classifier worker before anything it touches dies
    if (app.session) vivid::session::session_destroy(app.session);
    vgraph.shutdown();
    ui.shutdown();
    gpu.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
