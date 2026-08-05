#include "app/frame.h"
#include "app/perf_stats.h"   // ADR-0042: publish frame_ms/fps for the get_perf endpoint

#include "app/app.h"
#include "app/edit_gateway.h"   // ADR-0017 end_frame_audit
#include "app/input_internal.h"  // ADR-0022: mod_editor_drag (per-frame slider drag apply)
#include "platform/menu_bar.h"  // ADR-0017/G4 set_edit_labels; ADR-0018 set_document_edited
#include "platform/file_dialog.h" // ADR-0018 confirm_discard_changes (quit save-confirm)
#include "app/file_actions.h"     // ADR-0018 save() from the quit confirm
#include "app/autosave.h"         // ADR-0018 periodic autosave
#include "app/crash_recovery.h"   // ADR-0018 warm crash-attribution snapshot
#include <ctime>                  // ADR-0018 std::time for the autosave meta stamp
#include "app/window.h"
#include "app/project_paths.h"   // is_folder_project — derive the window-title name from the project path
#include "app/bridge_source.h"   // the audio→visual source-id grammar (shared with input.cpp's catalog)
#include "app/editor_window.h"   // UI-5: floated operator-editor window
#include "app/window_prefs.h"    // UI-5.4c: remembered float-window geometry
#include "app/video_recorder.h"  // realtime AV export: per-frame tick after end_frame
#include "gpu/gpu_context.h"
#include "gpu/gpu_util.h"
#include "ui/renderer_2d.h"
#include "ui/node_graph.h"
#include "ui/layout.h"
#include "ui/compound_widget.h"   // UI-4a: compound_span / xy_from_cursor / node_param_compound_rect
#include "ui/session_view.h"
#include "ui/audio_node_graph.h"   // AudioNodeGraph::graph_region for the node-reposition drag
#include "ui/mapping_overview.h"
#include "ui/shader_library_view.h"
#include "ui/diagnostics_panel.h"
#include "ui/toasts.h"
#include "ui/preset_popover.h"
#include "ui/clip_editor.h"
#include "transport.h"
#include "audio/vst3_host.h"
#include "audio/mini_fft.h"   // frame-side spectrum for the <src>.fft.k bridge sources
#include "operator_api/note_bus.h"   // publish each track's held notes for the note-instancer op
#include "operator_api/spectrum_bus.h"   // publish the master spectrum for per-band geometry ops
#include "operator_api/note_events.h"   // publish each track's discrete note on/off events for one-shot ops
#include "audio/vst3_plugin_window.h"
#include "audio/clap_plugin_window.h"
#include "app/plugin_editor_pool.h"   // Ph4 P1-01: close-all-and-null the editor pools
#include "audio/plugin_scan.h"   // plugin_scan_poll — drain background classifications
#include "gpu/visual_graph.h"
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

// The Output node's `display` param: 0 = Current (the monitor the app is on), 1 = Primary,
// 2 = Secondary (the first non-primary monitor). Falls back to a plain window when the requested
// monitor doesn't exist.
static GLFWmonitor* monitor_for_target(int target) {
    int mcount = 0; GLFWmonitor** mons = glfwGetMonitors(&mcount);
    if (mcount <= 0) return nullptr;
    if (target == 1) return glfwGetPrimaryMonitor();
    if (target == 2) {
        GLFWmonitor* prim = glfwGetPrimaryMonitor();
        for (int i = 0; i < mcount; ++i) if (mons[i] != prim) return mons[i];
        return nullptr;   // only one screen: no performance screen to take over
    }
    return (mcount > 1) ? mons[1] : nullptr;   // Current/default: the 2nd screen if there is one
}

void close_popout(App& app, Window& win) {
    if (!app.gpu || !win.popout) return;
    app.gpu->close_secondary();
    glfwDestroyWindow(win.popout);
    win.popout = nullptr; win.popout_fb_w = win.popout_fb_h = 0;
}

// Open/close the pop-out visuals window on the target display. Shares the wgpu device via a
// secondary surface.
void open_popout(App& app, Window& win, int display_target) {
    if (!app.gpu || win.popout) return;
    GLFWmonitor* mon = monitor_for_target(display_target);
    win.popout_display = display_target;
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

// ADR-0026: the Gemini-key entry modal — a centered panel with a masked field. Input (type / Enter /
// Esc) is handled in input.cpp while win.show_gemini_key; this only draws.
void draw_gemini_key_modal(Renderer2D& ui, Window& win) {
    const Style& s = style();
    const float w = 480.f, h = 156.f;
    const float x = (win.win_w - w) * 0.5f, y = (win.win_h - h) * 0.5f;
    overlay_panel(ui, { x, y, w, h }, "SET GEMINI KEY", s.gold, true,
                  { 0.f, 0.f, static_cast<float>(win.win_w), static_cast<float>(win.win_h) });
    // The masked field: a • per typed character, plus a caret. Empty → a dim placeholder.
    const float fx = x + 16.f, fy = y + 44.f, fw = w - 32.f, fh = 30.f;
    ui.draw_rect(fx, fy, fw, fh, 0.055f, 0.065f, 0.080f, 1.0f);
    ui.draw_rect_outline(fx, fy, fw, fh, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
    const float tx = fx + 10.f, ty = fy + 8.f;
    const bool caret_on = std::fmod(glfwGetTime(), 1.0) < 0.55;   // ~1 Hz blink — signals the field is focused
    if (win.gemini_key_buf.empty()) {
        ui.draw_text(tx, ty, "paste your key (\xE2\x8C\x98V), then press Enter",
                     s.dim[0], s.dim[1], s.dim[2], 0.8f, 0.82f);
        if (caret_on) ui.draw_rect(tx, ty - 1.f, 1.6f, 16.f, s.body[0], s.body[1], s.body[2], 0.9f);
    } else {
        std::string masked;
        masked.reserve(win.gemini_key_buf.size() * 3);
        for (size_t i = 0; i < win.gemini_key_buf.size(); ++i) masked += "\xE2\x80\xA2";  // •
        ui.draw_text(tx, ty, masked.c_str(), s.body[0], s.body[1], s.body[2], 1.0f, 0.82f);
        if (caret_on) {
            const float cw = ui.text_width(masked.c_str(), 0.82f);
            ui.draw_rect(tx + cw + 1.5f, ty - 1.f, 1.6f, 16.f, s.body[0], s.body[1], s.body[2], 0.9f);
        }
    }
    ui.draw_text(fx, y + 90.f, "Enter to save    \xC2\xB7    Esc to cancel",
                 s.dim[0], s.dim[1], s.dim[2], 1.0f, 0.80f);
    ui.draw_text(fx, y + 112.f, "Stored in your macOS Keychain (com.vivid.app) \xE2\x80\x94 never written to disk.",
                 s.dim[0], s.dim[1], s.dim[2], 1.0f, 0.72f);
}

// A tiny always-on performance read-out — smoothed FPS + frame time — pinned to the
// top-right corner over the node graph. Frame time is the wall-clock delta between
// successive calls (once per drawn frame), smoothed by an EMA (~30-frame time
// constant) so the digits don't jitter. The string is only re-formatted a few times a
// second, snprintf'd into a static buffer, so the draw path allocates nothing. The text
// tints green / gold / red as the frame rate drops, an at-a-glance load hint.
void draw_perf_hud(Renderer2D& ui, const Window& win) {
    static double last_t = glfwGetTime();
    static double ema_ms = 1000.0 / 60.0;   // smoothed frame time (ms), seeded at 60 fps
    static double refresh_acc = 0.25;       // seconds since the string was last formatted (force 1st)
    static char   buf[48] = "-- fps";
    const double now = glfwGetTime();
    double dt = now - last_t;
    last_t = now;
    dt = std::clamp(dt, 1e-4, 0.25);        // ignore stalls (resize / breakpoint / first frame)
    const double ms = dt * 1000.0;
    ema_ms += (ms - ema_ms) * 0.1;          // exponential moving average (alpha 0.1 ~= 30-frame mean)
    const double fps = ema_ms > 1e-4 ? 1000.0 / ema_ms : 0.0;
    vivid::perf::g_frame_ms.store(ema_ms, std::memory_order_relaxed);   // publish for the `get_perf` endpoint (ADR-0042)
    vivid::perf::g_fps.store(fps, std::memory_order_relaxed);
    refresh_acc += dt;
    if (refresh_acc >= 0.25) {              // re-format ~4x/sec so the digits read steadily
        refresh_acc = 0.0;
        std::snprintf(buf, sizeof buf, "%.0f fps \xC2\xB7 %.1f ms", fps, ema_ms);
    }
    const Style& s = style();
    const float scale = s.fs_value;         // small numeric read-out (0.70x)
    const float tw = ui.text_width(buf, scale);
    const float pad = 6.f, bh = 16.f;
    const float bw = tw + pad * 2.f;
    const float x = static_cast<float>(win.win_w) - bw - 8.f;
    const float y = 8.f;
    ui.draw_rect(x, y, bw, bh, s.recess[0], s.recess[1], s.recess[2], 0.60f);                         // semi-transparent chip
    ui.draw_rect_outline(x, y, bw, bh, 1.f, s.border_soft[0], s.border_soft[1], s.border_soft[2], 0.5f);
    const float* c = fps >= 55.0 ? s.green : (fps >= 30.0 ? s.gold : s.red);                          // load hint
    ui.draw_text(x + pad, y + 3.f, buf, c[0], c[1], c[2], 0.95f, scale);
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
    for (int k = 0; k < vivid::session::kMaxTracks; ++k)
        if (win.clap_win[k] && !clap_plugin_window_is_open(win.clap_win[k])) {
            clap_plugin_window_close(win.clap_win[k]); win.clap_win[k] = nullptr;
        }
}

// Ph4 P1-01: close EVERY open plugin-editor window and null its slot — unconditionally, unlike
// reap_plugin_windows (which only closes windows already self-reporting closed). Installed as
// App::before_audio_rebuild so a Full-tier undo/redo drops these raw handles into the plugin
// instances it is about to free, mirroring the manual track-removal path (input_clipgrid.cpp).
void close_plugin_editor_windows(Window& win) {
    close_editor_pool(win.track_win, [](Vst3PluginWindow* w) { vst3_plugin_window_close(w); });
    close_editor_pool(win.fx_win,    [](Vst3PluginWindow* w) { vst3_plugin_window_close(w); });
    close_editor_pool(win.clap_win,  [](ClapPluginWindow* w) { clap_plugin_window_close(w); });
}

void publish_bridge_sources(App& app, Window& win) {
    if (!app.transport || !app.graph) return;
    Transport& transport = *app.transport;
    NodeGraph& graph = *app.graph;
    namespace S = vivid::session;
    // Frame-side spectrum: snapshot a source's recent samples and publish <src>.fft.0..N-1. Static
    // scratch is fine — this runs once per frame on the UI thread. (The audio thread only paid for a
    // ring copy.) sr is fixed at 48k for the bin→Hz band mapping; a device-rate mismatch only nudges
    // band edges cosmetically.
    static float an_buf[S::kAnalysisN], an_re[S::kAnalysisN], an_im[S::kAnalysisN];
    namespace B = vivid::bridge;   // the one source-id grammar (ADR-0028)
    // ADR-0028: publish by INTERNED HANDLE. `H` resolves a source's handle from the per-window cache by a
    // cheap integer identity, building the id string only on the first frame a source appears; thereafter
    // the hot path is an integer lookup + a pointer write (graph.publish) — no per-frame string or hash.
    auto H = [&](uint64_t k, auto&& build) -> int {
        auto it = win.bridge_handles.find(k);
        if (it != win.bridge_handles.end()) return it->second;
        const int h = graph.source_handle(build());
        win.bridge_handles.emplace(k, h);
        return h;
    };
    // identity key: tag (top byte) | track_id (24b) | node_id (24b) | kind/band (8b) — collision-free.
    auto key = [](uint64_t tag, uint64_t a, uint64_t b, uint64_t c) { return (tag << 56) | (a << 32) | (b << 8) | c; };
    constexpr uint64_t kMaster = 1, kMasterFft = 2, kTrack = 3, kTrackFft = 4, kNodeRms = 5, kNodeCtl = 6, kNodeFft = 7, kNodeFftGate = 8, kTransport = 9;
    // Frame-side spectrum: snapshot recent samples (already in an_buf), publish <src>.fft.0..N-1 by handle.
    // Static scratch is fine — once per frame on the UI thread. sr fixed at 48k (a device-rate mismatch
    // only nudges band edges cosmetically).
    auto do_fft = [&](int nsamp, auto&& publish_band) {
        float bands[S::kFftBands];
        vivid::audio::spectrum_log_bands(an_buf, nsamp, 48000.f, bands, S::kFftBands, an_re, an_im);
        for (int k = 0; k < S::kFftBands; ++k) publish_band(k, bands[k]);
    };
    constexpr auto rel = std::memory_order_relaxed;
    const float level = transport.level.load(rel);
    win.react += (std::min(1.0f, level * 5.0f) - win.react) * 0.3f;
    win.trHold *= 0.85f;
    win.trHold = std::max(win.trHold, transport.transient.load(rel));
    // Master scalars (kinds 0..4); the kind-suffix table is the grammar's single source of truth.
    const float mvals[5] = { win.react, std::min(1.0f, win.trHold),
                             std::min(1.0f, transport.band_low.load(rel) * 5.0f),
                             std::min(1.0f, transport.band_mid.load(rel) * 8.0f),
                             std::min(1.0f, transport.band_high.load(rel) * 12.0f) };
    for (int k = 0; k < 5; ++k)
        graph.publish(H(key(kMaster, 0, 0, k), [k] { return B::master_source(B::kTrackKindSuffixes[k]); }), mvals[k]);
    // Transport (beat/bar/tempo) sources — musically-timed signals so visuals can punctuate ON the
    // beat/bar instead of only following loudness. beat/bar_phase are 0..1 sawtooth ramps; downbeat/
    // beat_pulse are decayed flashes snapped to 1 on each bar/beat edge (edge-detected here).
    {
        const double beats = transport.beats.load(rel);
        const int bpb = std::max(1, transport.beats_per_bar.load(rel));
        const double bar_pos = beats / static_cast<double>(bpb);
        const long long beat_idx = static_cast<long long>(std::floor(beats));
        const long long bar_idx  = static_cast<long long>(std::floor(bar_pos));
        win.beatPulse *= 0.85f;
        if (beat_idx != win.lastBeatIdx) { win.beatPulse = 1.f; win.lastBeatIdx = beat_idx; }
        win.barPulse *= 0.85f;
        if (bar_idx != win.lastBarIdx)   { win.barPulse = 1.f; win.lastBarIdx = bar_idx; }
        const float tvals[B::kNumTransportKinds] = {
            static_cast<float>(beats - std::floor(beats)),        // beat: phase within the beat
            static_cast<float>(bar_pos - std::floor(bar_pos)),    // bar_phase: phase within the bar
            std::min(1.0f, win.barPulse),                          // downbeat pulse
            std::min(1.0f, win.beatPulse) };                       // beat pulse
        for (int k = 0; k < B::kNumTransportKinds; ++k)
            graph.publish(H(key(kTransport, 0, 0, k), [k] { return B::transport_source(B::kTransportKindSuffixes[k]); }), tvals[k]);
    }
    if (app.session) if (int nm = S::session_master_analysis_copy(app.session, an_buf, S::kAnalysisN); nm > 1) {
        do_fft(nm, [&](int k, float v) { graph.publish(H(key(kMasterFft, 0, 0, k), [k] { return B::master_fft(k); }), v); });
        // Richer master spectrum for the spectrum bus (visual ops → per-band geometry: 3D equaliser /
        // spectral ridge), at higher band resolution than the 8-band scalar bridge above. Publish the
        // RAW magnitudes normalised to a slowly-decaying peak — a fixed dB reference (spectrum_log_bands)
        // saturates every band to 1.0 on a loud mix, giving a flat, static equaliser; normalising to the
        // running peak keeps the RELATIVE shape (which bands are loud right now) so the bars actually move.
        constexpr int kBusBands = 48;
        static float sbands[kBusBands];
        vivid::audio::spectrum_raw_bands(an_buf, nm, 48000.f, sbands, kBusBands, an_re, an_im);
        // PER-BAND normalisation: each band relative to its OWN slowly-decaying peak. Global-peak
        // normalisation would let the (much louder) kick crush every other band to ~0; per-band lets a
        // quiet hat band still use its full 0..1 range, so every bar with real content moves.
        static float s_band_peak[kBusBands] = {0};
        for (int k = 0; k < kBusBands; ++k) {
            s_band_peak[k] = std::max(s_band_peak[k] * 0.992f, sbands[k]);   // rise fast, fall slow (~2s)
            sbands[k] = (s_band_peak[k] > 1e-5f) ? std::min(1.f, sbands[k] / s_band_peak[k]) : 0.f;
        }
        vivid_spectrum_bus_publish_master(sbands, kBusBands);
    }
    int ntracks = 0;   // live tracks published to the note bus this frame (the rest are freed below)
    for (int t = 0; app.session && t < vivid::session::session_track_count(app.session) && t < vivid::session::kMaxTracks; ++t) {
        const float lv = vivid::session::session_track_level(app.session, t);
        win.trkReact[t] += (std::min(1.0f, lv * 5.0f) - win.trkReact[t]) * 0.3f;
        win.trkTrHold[t] *= 0.85f;
        win.trkTrHold[t] = std::max(win.trkTrHold[t], vivid::session::session_track_transient(app.session, t));
        win.trkNoteHold[t] *= 0.85f;   // decay the note-on flash before reading it into the gate source below
        win.trkNoteHold[t] = std::max(win.trkNoteHold[t], vivid::session::session_track_note_gate(app.session, t));
        const uint64_t tid = static_cast<uint64_t>(vivid::session::session_track_id(app.session, t));
        // Track scalars (kinds 0..7). 5/6/7 = note/velocity/gate: pitch + velocity are already 0..1 and
        // HELD (a sustained note keeps its colour); gate is the note-on flash decayed above.
        const float tvals[8] = { win.trkReact[t], std::min(1.0f, win.trkTrHold[t]),
                                 std::min(1.0f, vivid::session::session_track_band(app.session, t, 0) * 5.0f),
                                 std::min(1.0f, vivid::session::session_track_band(app.session, t, 1) * 8.0f),
                                 std::min(1.0f, vivid::session::session_track_band(app.session, t, 2) * 12.0f),
                                 vivid::session::session_track_note_pitch(app.session, t),
                                 vivid::session::session_track_note_velocity(app.session, t),
                                 std::min(1.0f, win.trkNoteHold[t]) };
        for (int k = 0; k < 8; ++k)
            graph.publish(H(key(kTrack, tid, 0, k), [tid, k] { return B::track_source(static_cast<int>(tid), B::kTrackKindSuffixes[k]); }), tvals[k]);
        if (int ns = S::session_track_analysis_copy(app.session, t, an_buf, S::kAnalysisN); ns > 1)
            do_fft(ns, [&](int k, float v) { graph.publish(H(key(kTrackFft, tid, 0, k), [tid, k] { return B::track_fft(static_cast<int>(tid), k); }), v); });
        // Per-audio-graph-node sources. RMS (node_<t>_<nid>.rms) is always-on + cheap (from the scope
        // ring). FFT (node_<t>_<nid>.fft.k) is CONNECTION-GATED: only nodes whose fft source is consumed
        // (wired/spawned) get a capture bit set + get FFT'd — so an unwatched node costs nothing.
        const int nn = S::session_track_audio_graph_node_count(app.session, t);
        uint64_t analyze_mask = 0;
        for (int i = 0; i < nn; ++i) {
            const int nidi = S::session_track_audio_graph_node_id(app.session, t, i);
            if (nidi < 0) continue;
            const uint64_t nid = static_cast<uint64_t>(nidi);
            float sc[256];
            const int nsc = S::session_track_audio_graph_node_scope(app.session, t, i, sc, 256);
            double ss = 0; for (int j = 0; j < nsc; ++j) ss += static_cast<double>(sc[j]) * sc[j];
            const float rms = nsc > 0 ? static_cast<float>(std::sqrt(ss / nsc)) : 0.f;
            graph.publish(H(key(kNodeRms, tid, nid, 0), [tid, nid] { return B::node_prefix(static_cast<int>(tid), static_cast<int>(nid)) + ".rms"; }),
                          std::min(1.0f, rms * 5.0f));
            // A modulator/LFO node emits a 0..1 CONTROL signal (no audio) as node_<...>.ctl — an LFO can
            // drive a visual param directly. Cheap (an atomic read), so always-on like rms.
            if (S::session_track_audio_graph_node_kind(app.session, t, i) == 5)   // 5 = NativeMod
                graph.publish(H(key(kNodeCtl, tid, nid, 0), [tid, nid] { return B::node_prefix(static_cast<int>(tid), static_cast<int>(nid)) + ".ctl"; }),
                              S::session_track_audio_graph_node_control_out(app.session, t, i));
            if (i < 64 && graph.consumed(H(key(kNodeFftGate, tid, nid, 0), [tid, nid] { return B::node_prefix(static_cast<int>(tid), static_cast<int>(nid)) + ".fft"; }))) {
                analyze_mask |= (uint64_t(1) << i);   // gated: capture + FFT only when watched
                if (int nns = S::session_track_node_analysis_copy(app.session, t, nidi, an_buf, S::kAnalysisN); nns > 1)
                    do_fft(nns, [&](int k, float v) { graph.publish(H(key(kNodeFft, tid, nid, k),
                        [tid, nid, k] { return B::node_prefix(static_cast<int>(tid), static_cast<int>(nid)) + ".fft." + std::to_string(k); }), v); });
            }
        }
        S::session_set_track_node_analyze_mask(app.session, t, analyze_mask);
        // Publish this track's held notes to the active-notes bus for a note-instancer GPU op. Into
        // position slot `t`, TAGGED with the stable id `tid` — so the op (which addresses by stable id)
        // follows the track across reorder/delete, like every other source above.
        S::ActiveNote an[S::kMaxHeld];
        const int na = S::session_track_active_notes(app.session, t, an, S::kMaxHeld);
        VividActiveNote vn[VIVID_MAX_ACTIVE_NOTES];
        const int m = std::min(na, VIVID_MAX_ACTIVE_NOTES);
        for (int k = 0; k < m; ++k) { vn[k].pitch = an[k].pitch; vn[k].velocity = an[k].vel; }
        vivid_note_bus_publish(t, static_cast<int>(tid), vn, static_cast<uint32_t>(m));
        // Also DRAIN this frame's discrete note on/off events into the note-event bus (one-shot ops).
        // The drain is destructive, so this is the single per-frame consumer of the track's event ring.
        S::NoteEvt ne[VIVID_MAX_NOTE_EVENTS];
        const int nne = S::session_track_note_events(app.session, t, ne, VIVID_MAX_NOTE_EVENTS);
        VividNoteHit vev[VIVID_MAX_NOTE_EVENTS];
        for (int k = 0; k < nne; ++k) {
            vev[k].kind = ne[k].kind; vev[k].pitch = ne[k].pitch;
            vev[k].velocity = ne[k].vel; vev[k].note_id = ne[k].note_id;
        }
        vivid_note_event_bus_publish(t, static_cast<int>(tid), vev, static_cast<uint32_t>(nne));
        ntracks = t + 1;
    }
    for (int s = ntracks; s < VIVID_NOTE_BUS_TRACKS; ++s) {   // free slots no live track occupies this frame
        vivid_note_bus_publish(s, -1, nullptr, 0);
        if (s < VIVID_NOTE_EVENT_TRACKS) vivid_note_event_bus_publish(s, -1, nullptr, 0);
    }
    // Advance mapping smoothing (envelope followers) once per frame BEFORE resolving params, using a
    // real wall-clock delta clamped against stalls. Sources are all published above at this point.
    static double s_prev_bridge_t = glfwGetTime();
    const double now = glfwGetTime();
    const float bridge_dt = static_cast<float>(std::clamp(now - s_prev_bridge_t, 0.0, 0.1));
    s_prev_bridge_t = now;
    graph.advance_mappings(bridge_dt);
    graph.apply_params();
}

void apply_audio_param_mappings(App& app) {
    if (!app.session || !app.graph) return;
    std::set<std::string> active;   // gnode dests driven this frame (for disconnect detection below)
    for (const auto& m : app.graph->mappings()) {
        int T = -1, D = 0, I = 0;
        if (m.dest.rfind("param:", 0) == 0) {                 // legacy VST3 linear-DEVICE param (pre-ADR-0022;
            if (std::sscanf(m.dest.c_str(), "param:%d:%d:%d", &T, &D, &I) == 3 && T >= 0)   // not a graph node, so
                vivid::session::session_set_param(app.session, T, D,                        // left on the old path)
                                             vivid::session::session_param_id(app.session, T, D, I),
                                             app.graph->dest_value(m.dest));
        } else if (m.dest.rfind("aparam:", 0) == 0) {         // legacy native linear-DEVICE param (see above)
            if (std::sscanf(m.dest.c_str(), "aparam:%d:%d:%d", &T, &D, &I) == 3 && T >= 0) {
                const float lo = vivid::session::session_audio_op_param_min(app.session, T, D, I);
                const float hi = vivid::session::session_audio_op_param_max(app.session, T, D, I);
                vivid::session::session_audio_op_param_set(app.session, T, D, I,
                                             lo + std::clamp(app.graph->dest_value(m.dest), 0.f, 1.f) * (hi - lo));
            }
        } else if (m.dest.rfind("gnode:", 0) == 0) {          // audio-graph node param (by STABLE node id)
            int NID = -1;
            if (std::sscanf(m.dest.c_str(), "gnode:%d:%d:%d", &T, &NID, &I) == 3 && T >= 0) {
                const float lo = vivid::session::session_audio_graph_node_param_min(app.session, T, NID, I);
                const float hi = vivid::session::session_audio_graph_node_param_max(app.session, T, NID, I);
                // ADR-0030 P2: DELIVER (non-destructive) instead of the base setter — the mapped value
                // is heard without overwriting the user's authored knob.
                vivid::session::session_audio_graph_node_param_deliver(app.session, T, NID, I,
                                             lo + std::clamp(app.graph->dest_value(m.dest), 0.f, 1.f) * (hi - lo));
                active.insert(m.dest);
            }
        }
    }
    // ADR-0030 P2: a gnode dest we drove last frame but not this one was disconnected — clear its
    // override once so the node returns to its authored base (native drops the override; a plugin
    // gets the captured base re-delivered).
    for (const std::string& d : app.bridge_active_audio_dests) {
        if (active.count(d)) continue;
        int T = -1, NID = -1, I = 0;
        if (std::sscanf(d.c_str(), "gnode:%d:%d:%d", &T, &NID, &I) == 3 && T >= 0)
            vivid::session::session_audio_graph_node_param_override_clear(app.session, T, NID, I);
    }
    app.bridge_active_audio_dests.swap(active);
}

void update_drag_continuations(App& app, Window& win, double mx, double my) {
    win.cur_x = mx; win.cur_y = my;   // latest cursor (used by the audio-graph ghost wire)
    // Audio-graph drag continuations (pan / param knob / node reposition / key range) — the editor
    // owns them (ADR-0023 6d). Mutually exclusive with the shell drags below, so order is immaterial.
    if (app.audio_graph) app.audio_graph->on_move(app, win, mx, my);
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
    // ADR-0014: the floating output preview — drag the header to move it, the corner grip to size it
    // (width only; the height follows the output's aspect). Kept loosely inside the visuals column so
    // it can't be lost off-screen.
    if (win.preview.dragging || win.preview.resizing) {
        if (win.preview.resizing) {
            win.preview.w = static_cast<float>(mx - win.preview.grab_x);
        } else {
            win.preview.x = static_cast<float>(mx - win.preview.grab_x);
            win.preview.y = static_cast<float>(my - win.preview.grab_y);
        }
        win.preview.clamp(win.visuals_panel());   // one authority for "the preview stays inside the column"
    }
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
            if (app.edit_gateway) app.edit_gateway->note_edit("Edit Clip", "clip-edit");   // ADR-0017/G3
        }
        if (!win.editor->is_audio() && app.session && win.editor->take_loop_dirty()) {   // in-clip loop edit
            double ls, le; win.editor->loop_range(ls, le);
            vivid::session::session_set_clip_loop(app.session, win.editor->track(), win.editor->scene(), ls, le);
            if (app.edit_gateway) app.edit_gateway->note_edit("Set Loop", "clip-loop");
        }
        // A5: apply audio warp/pitch/auto-warp requests from the editor header, then refresh
        // the editor's marker + shape display from the engine.
        if (win.editor->is_audio() && app.session) {
            const int req = win.editor->take_audio_req();   // 1 shaping, 2 auto, 4 warp-pts, 8 slice
            if (req) {
                if (app.edit_gateway) app.edit_gateway->note_edit("Warp Clip", "clip-warp");   // ADR-0017/G3
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
        if (app.edit_gateway) app.edit_gateway->note_edit("Set Gain", "gain-drag");   // ADR-0017/G3
    }
    if (win.param_drag >= 0) {
        if (app.edit_gateway) app.edit_gateway->note_edit("Adjust Param", "param-drag");  // folds into the gesture
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
        if (win.param_is_node && app.graph)   // visual-node param drag (the linear device drag was retired)
            app.graph->set_op_param_base_at(app.graph->selected_op(), win.param_drag, v);
    }
    // ADR-0022: dragging a mod-editor slider (amount / curve).
    if (win.mod_ed_drag >= 0) vivid::input::mod_editor_drag(win, app, mx, my);
}

// ADR-0016 / S4 — the shader library's hot-reload, applied on the main thread.
//
// The library absorbs a BODY edit itself (every live node recompiles its pipeline in place,
// keeping the last good one if the edit doesn't compile). An INTERFACE edit — a param added,
// removed, retyped or re-ranged — is the one the graph has to hear about: the nodes' param
// storage no longer matches the file, so they are rebuilt, and their values are carried over
// BY NAME (VisualGraph::rebuild_op_instances -> VisualNode::stash).
void apply_shader_reloads(App& app) {
    if (!app.vgraph) return;
    // ADR-0020: shader edits are as visible as operator edits — successes log "reloaded", a compile
    // failure rides a toast + the log view while the last-good version keeps running.
    auto shader_err = [&](const std::string& name) -> std::string {
        for (const auto& e : app.shader_library.entries()) if (e.name == name) return e.error;
        return {};
    };
    for (const ShaderReload& r : app.shader_library.poll(app.op_registry)) {
        switch (r.change) {
            case ShaderChange::Interface: {
                const int n = app.vgraph->rebuild_op_instances(r.name);
                VLOG_INFO(app, "shader '%s' header changed — %d node(s) rebuilt (values kept by name)",
                          r.name.c_str(), n);
                break;
            }
            case ShaderChange::Added:
                VLOG_INFO(app, "shader '%s' added — press Tab to spawn it", r.name.c_str());
                break;
            case ShaderChange::Body:
                VLOG_INFO(app, "shader '%s' reloaded", r.name.c_str());   // node already recompiled itself
                break;
            case ShaderChange::Failed: {
                const std::string e = shader_err(r.name);
                VLOG_ERR(app, "shader '%s' failed to compile: %s (last good still running)",
                         r.name.c_str(), e.empty() ? "see log" : e.c_str());
                break;
            }
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
        // ADR-0018: intercept a quit request with unsaved changes — confirm before the loop exits.
        // Save (aborts the quit if the save dialog is cancelled) / Don't Save (quit) / Cancel (stay).
        if (glfwWindowShouldClose(window) && app.edit_gateway && app.edit_gateway->dirty()) {
            switch (vivid::platform::confirm_discard_changes()) {
                case vivid::platform::DiscardChoice::Save:
                    vivid::file_actions::save(window, win, app);
                    if (app.edit_gateway->dirty()) glfwSetWindowShouldClose(window, GLFW_FALSE);  // save cancelled → stay
                    break;
                case vivid::platform::DiscardChoice::Discard: break;                              // allow quit
                case vivid::platform::DiscardChoice::Cancel:  glfwSetWindowShouldClose(window, GLFW_FALSE); break;
            }
        }
        return !glfwWindowShouldClose(window);
    };
    bool undo_baseline_seeded = false;   // ADR-0017: seed the baseline after the first laid-out frame
    int  reseed_settle_frames = 0;       // Phase-2 F1: frames spent waiting for a post-open reseed to settle
    unsigned last_undo_rev = ~0u;        // ADR-0017/G4: refresh Edit-menu labels when history changes
    int  last_dirty = -1;                // ADR-0018: push the macOS edited-dot when dirty state flips
    int  last_export_rec = -1;           // flip the File > Export Video menu label with recording state
    std::string last_title;              // window title = current project name; re-set only when it changes
    double last_autosave = 0.0;          // ADR-0018: throttle periodic autosave (glfwGetTime seconds)
    double last_snapshot = 0.0;          // ADR-0018: throttle the crash-attribution warm snapshot
    auto tick = [&]() -> bool {
        if (glfwWindowShouldClose(window)) return false;
        cctx.session = app.session;
        control.process_pending(cctx);   // apply queued MCP commands on the main thread
        // Keep the File > Export Video menu label in sync with recording state, whatever started
        // it (menu / MCP / timed auto-stop). Cheap flip-detected update.
        if (app.recorder) {
            const int rec = app.recorder->is_recording() ? 1 : 0;
            if (rec != last_export_rec) {
                last_export_rec = rec;
                vivid::platform::set_export_video_recording(rec != 0);
            }
        }
        if (app.session) vivid::session::session_poll_plugin_loads(app.session);   // apply finished async CLAP loads
        // UX Ph6 F2: a demo opened without its instrument plugins (Surge XT, Cassette Drums, …) degrades
        // those tracks to silence — logged only to stderr until now. Once every async CLAP load has
        // settled (pending()==0), surface ONE consolidated toast naming the missing plugins so the
        // silence is explained. Draining clears the tally, so it fires exactly once per load.
        if (app.session && vivid::session::session_plugin_loads_pending(app.session) == 0) {
            const int nmiss = vivid::session::session_unresolved_instrument_count(app.session);
            if (nmiss > 0) {
                std::string names;
                for (int i = 0; i < nmiss; ++i) {
                    if (i) names += ", ";
                    names += vivid::session::session_unresolved_instrument_name(app.session, i);
                }
                std::string msg = (nmiss == 1 ? "Missing plugin: " : "Missing plugins: ") + names
                    + (nmiss == 1 ? " \xE2\x80\x94 its track plays silent. Install it to hear this project."
                                  : " \xE2\x80\x94 those tracks play silent. Install them to hear this project.");
                VLOG_WARN(app, "%s", msg.c_str());   // ADR-0019: also lands in the log view + header dot
                vivid::ui::push_toast(win.toasts, vivid::LogLevel::Warning, msg, glfwGetTime(), 12.0);
                vivid::session::session_clear_unresolved_instruments(app.session);
            }
        }
        vivid::session::plugin_scan_poll();   // apply finished plugin classifications (browser badges)
        app.hot_reload.tick();           // apply any ready operator hot-swaps (main thread)
        apply_shader_reloads(app);       // ADR-0016: pick up edits to shader FILES (main thread)
        app.log.drain_rt();              // ADR-0019 (E4): move audio-thread log events onto the UI thread
        // Surface new Error-level log events as toasts (severity-gated: Warning stays the passive
        // header dot, Info/Debug go only to the log view). One toast per new Error since last frame.
        {
            const double tnow = glfwGetTime();
            for (const auto& e : app.log.entries()) {
                if (e.id <= win.last_toast_id || e.level != vivid::LogLevel::Error) continue;
                vivid::ui::push_toast(win.toasts, e.level, e.msg, tnow);
            }
            win.last_toast_id = app.log.next_id() - 1;
        }
        // ADR-0026: an "Evaluate Output" job (Eval menu) → toast its verdict when it lands. Polled on
        // the UI thread; the Gemini call itself ran async off it, so this never blocks.
        if (win.music_eval_job >= 0) {
            const auto r = app.music_eval.poll(win.music_eval_job);
            const std::string st = r.value("status", std::string("pending"));
            if (st != "pending") {
                win.music_eval_job = -1;
                std::string msg;
                vivid::LogLevel lvl = vivid::LogLevel::Info;
                if (st == "done") {
                    msg = "Eval: ";
                    const std::string k = r.value("key", std::string());
                    if (!k.empty()) msg += k + "  \xC2\xB7  ";
                    if (r.contains("tempo_bpm") && r["tempo_bpm"].is_number()) {
                        char b[24]; std::snprintf(b, sizeof b, "%.0f BPM  \xC2\xB7  ", r["tempo_bpm"].get<double>());
                        msg += b;
                    }
                    msg += r.value("summary", std::string());
                } else {
                    lvl = vivid::LogLevel::Warning;
                    msg = "Eval failed: ";
                    if (r.contains("error") && r["error"].contains("message"))
                        msg += r["error"]["message"].get<std::string>();
                }
                vivid::ui::push_toast(win.toasts, lvl, msg, glfwGetTime(), 12.0);
            }
        }

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
        win.health = collect_health(app);   // ADR-0019: one snapshot/frame; the dot + panel both read it
        // Ph3 P2-01: WGPU device loss is contained (the loop keeps running) but renders nothing — the
        // window just freezes. Surface it ONCE with a NATIVE alert; a GPU-rendered toast can't show
        // with a dead device, so without this the user stares at a frozen window with no explanation.
        if (app.gpu && app.gpu->device_lost()) {
            static bool device_loss_notified = false;
            if (!device_loss_notified) {
                device_loss_notified = true;
                VLOG_ERR(app, "GPU device lost — rendering has stopped. Please quit and reopen Vivid.");
                vivid::platform::show_alert("GPU device lost",
                    "Vivid's graphics device was lost and rendering has stopped. Please quit and reopen "
                    "Vivid. Your work is autosaved.");
            }
        }
        const double beats = transport.beats.load(std::memory_order_relaxed);
        publish_bridge_sources(app, win);
        apply_audio_param_mappings(app);

        double mx, my; glfwGetCursorPos(window, &mx, &my);
        update_drag_continuations(app, win, mx, my);

        const float tsec = static_cast<float>(glfwGetTime());
        FrameState frame;
        if (gpu.begin_frame(frame)) {
            // ADR-0014: run the chain into the node RTs FIRST (so this frame's node thumbnails are
            // current), but do NOT present yet — the output is blitted over the graph further down,
            // because the preview floats above the canvas.
            clear_pass(frame.encoder, frame.view, 0.045f, 0.05f, 0.06f);  // static dark backdrop
            vgraph.set_metronome(static_cast<float>(transport.bpm.load(std::memory_order_relaxed)),
                                 transport.beats_per_bar.load(std::memory_order_relaxed), beats);
            vgraph.run_chain(frame.encoder, tsec);
            win.preview.out_aspect = vgraph.rt_aspect();   // cache: drives the preview's height + hit-rects
            win.preview.clamp(win.visuals_panel());        // ...so a new aspect can resize it out of bounds
            // ADR-0014: WHERE the output is shown is also the Output node's business. Reconcile the
            // node's params with the actual window state each frame — the params are the truth, the
            // UI buttons just write to them (and so do MCP / the control server, for free).
            if (vgraph.output_index() >= 0) {
                win.preview.show = vgraph.output_param("preview", 1.f) > 0.5f;
                const bool want_out = vgraph.output_param("launch", 0.f) > 0.5f;
                const int  disp = static_cast<int>(std::lround(vgraph.output_param("display", 0.f)));
                if (want_out && !win.popout)                      open_popout(app, win, disp);
                else if (!want_out && win.popout)                 close_popout(app, win);
                else if (want_out && win.popout && disp != win.popout_display) {
                    close_popout(app, win); open_popout(app, win, disp);   // moved to another screen
                }
            }

            draw_ui(ui, win, beats, mx, my);
            // ADR-0014: the visuals node graph IS the visual zone — it owns the whole right column,
            // always drawn, no reveal toggle.
            { const Rect g = win.visuals_panel();
              graph.set_bounds(g.x + 8.f, g.y + 26.f, g.x + g.w - 8.f, g.y + g.h - 8.f);  // inset node-layout bounds
              // Grid + clip fill the whole right column edge-to-edge. visuals_panel is inset by
              // kPaneMargin on every side, so expand back out to the true column (transport->dock,
              // split->window edge). The left stops just clear of the splitter strip (split_x+3) so
              // the grid doesn't paint over the divider — the graph draws after the splitter.
              const float m = ui::kPaneMargin;
              graph.set_frame(g.x - m + 3.f, g.y - m, g.x + g.w + m, g.y + g.h + m);
              graph.draw(ui); }   // includes live node thumbnails via draw_texture
            // UI-1: recompute the detail region's explicit focus — the single source of truth
            // for what the bottom region shows + its domain — replacing the old implicit race
            // where draw and input each re-derived the mode from the current selection.
            {
                FocusContext f;
                const int selop = app.graph ? app.graph->selected_op() : -1;
                if (clip_editor.is_open() && clip_editor.is_docked()) {
                    f.kind = FocusContext::Kind::ClipEditor; f.dom = FocusContext::Dom::Audio;
                    f.track = clip_editor.track(); f.scene = clip_editor.scene();
                } else if (selop >= 0 && win.show_op_editor && app.graph->op_has_editor(selop)) {
                    f.kind = FocusContext::Kind::OpEditor; f.dom = FocusContext::Dom::Visual; f.node = selop;   // UI-4b
                } else if (selop >= 0) {
                    f.kind = FocusContext::Kind::VisualNode; f.dom = FocusContext::Dom::Visual; f.node = selop;
                } else {   // a track's default detail view IS its audio node graph (supersedes the linear chain)
                    f.kind = FocusContext::Kind::AudioGraph; f.dom = FocusContext::Dom::Audio; f.track = win.sel_track;
                }
                win.focus = f;
            }
            // The bottom dock is the detail region: the clip editor owns it while docked;
            // otherwise it shows the focused device (audio) or visual-node (visual) inspector.
            if (win.focus.kind != FocusContext::Kind::ClipEditor) draw_device_dock(ui, win, beats, mx, my);
            // Pass 1: DAW + node graph (cards + thumbnails composite in-batch).
            ui.flush(frame.encoder, frame.view, win.win_w, win.win_h, win.fb_w, win.fb_h);
            // The floating OUTPUT preview: blitted OVER the graph canvas (a GPU pass recorded after
            // pass 1's, so it lands on top), with its chrome drawn above it in pass 2.
            if (win.preview.show) {
                const Rect vp = win.preview.viewer();
                vgraph.present_to(frame.encoder, frame.view, vp.x * win.dpi, vp.y * win.dpi,
                                  vp.w * win.dpi, vp.h * win.dpi,
                                  static_cast<float>(win.fb_w), static_cast<float>(win.fb_h),
                                  tsec, /*clear*/false);
            }
            // Pass 2: floating overlays — drawn AFTER pass 1 so they sit on top.
            if (win.preview.show) draw_output_preview(ui, win, mx, my);
            graph.draw_overlays(ui);      // the visuals Tab chooser
            win.audio_chooser.draw(ui);   // the audio Tab chooser (A3) — same widget, one catalog
            win.generator_chooser.draw(ui);   // ADR-0050: the add-generator picker (rows draw CATALOG previews)
            draw_popup(ui, win.menu);       // ADR-0027: characteristics menu (header baked in at open)
            draw_popup(ui, win.map_menu);   // ADR-0027: bridge map-source picker
            draw_mod_editor(ui, win.mod_editor, app.session, win.sel_track);   // ADR-0022 shape editor
            draw_popup(ui, win.node_menu);        // ADR-0027: op-node menu (open/fork/clone)
            draw_popup(ui, win.audio_node_menu);  // ADR-0027: audio-graph node "→ visuals"
            draw_popup(ui, win.param_menu);       // node param-curation menu (show/hide + wire-reveal)
            win.param_chooser.draw(ui);   // Phase 2c: the curated-inspector "+ Add param" palette (modal, on top)
            clip_editor.set_playhead(beats);
            clip_editor.set_hover(mx, my);   // ADR-0048: passive cursor → inspector control + handle hover
            clip_editor.draw(ui);  // editor window on top
            // The docked clip editor fills the dock and skips draw_device_dock (which paints the resize
            // strip), so draw the strip here — otherwise the dock's resize handle vanishes while editing.
            if (clip_editor.is_open() && clip_editor.is_docked()) {
                const Rect dr = win.dock_resize_rect();
                dock_resize_strip(ui, 0.f, win.dock_top(), static_cast<float>(win.win_w),
                                  hit(dr, mx, my) || win.dock_drag);
            }
            if (win.show_mappings) draw_mapping_overview(ui, app.graph, app.session, win.win_w, win.win_h);
            if (win.show_shader_library) draw_shader_library_view(ui, app.shader_library, win.win_w, win.win_h);
            if (win.show_diagnostics) draw_diagnostics_panel(ui, win.health, app, win.win_w, win.win_h);
            if (win.show_log) draw_log_view(ui, app.log, win.win_w, win.win_h);
            if (win.show_shortcuts) draw_shortcuts_overlay(ui, win.win_w, win.win_h);   // Ph4 F3
            if (win.show_gemini_key) draw_gemini_key_modal(ui, win);   // ADR-0026 key entry (on top)
            // UX Ph4 F3: a keyboard wire is pending — remind the user how to commit / cancel it.
            if (win.kbd_wire_dom)
                ui.draw_text(16.f, static_cast<float>(win.win_h) - 30.f,
                             "Wiring \xE2\x80\x94 select a target node, then W to connect  (Esc cancels)",
                             0.98f, 0.80f, 0.30f, 1.0f, 0.82f);
            draw_toasts(ui, win.toasts, glfwGetTime(), win.win_w, win.win_h);
            if (win.show_presets) draw_preset_popover(ui, app, win.presets_node, win.win_w, win.win_h);
            draw_perf_hud(ui, win);   // always-on FPS / frame-time read-out, drawn last (on top)
            ui.flush(frame.encoder, frame.view, win.win_w, win.win_h, win.fb_w, win.fb_h);
            gpu.end_frame(frame);
            // Video export (realtime): this frame's Output RT is now submitted to the queue, so
            // read it back + drain the audio tap into the AV writer. Only touches the GPU while a
            // recording is active. A true return means a timed export just auto-stopped — toast it.
            if (app.recorder && app.recorder->is_recording()) {
                std::vector<uint8_t> rgba; uint32_t rw = 0, rh = 0;
                const bool got = vgraph.read_output_pixels(rgba, rw, rh);
                if (app.recorder->tick(got ? rgba.data() : nullptr, rw, rh, transport)) {
                    const auto st = app.recorder->status();
                    char m[192];
                    std::snprintf(m, sizeof m, "Video exported: %s (%llu frames, %.1fs)",
                                  st.path.c_str(), static_cast<unsigned long long>(st.frames), st.elapsed_sec);
                    vivid::ui::push_toast(win.toasts, vivid::LogLevel::Info, m, glfwGetTime(), 10.0);
                }
            }
        }

        // Pop-out visuals window: mirror the current output onto its surface, fullscreen.
        // (The graph rendered once above; this only re-blits the output FBO.)
        if (win.popout) {
            // Closed from the OS side (its Esc / the window manager): write `launch` back off, so
            // the Output node never claims a window that isn't there.
            if (glfwWindowShouldClose(win.popout)) {
                close_popout(app, win);
                vgraph.set_output_param("launch", 0.f);
            }
            else if (gpu.has_secondary()) {
                int fbw = 0, fbh = 0; glfwGetFramebufferSize(win.popout, &fbw, &fbh);
                if (fbw > 0 && fbh > 0) {
                    if (fbw != win.popout_fb_w || fbh != win.popout_fb_h) {
                        gpu.resize_secondary(static_cast<uint32_t>(fbw), static_cast<uint32_t>(fbh));
                        win.popout_fb_w = fbw; win.popout_fb_h = fbh;
                    }
                    FrameState f2;
                    if (gpu.begin_secondary(f2)) {
                        vgraph.present_to(f2.encoder, f2.view, 0.f, 0.f,
                                          static_cast<float>(fbw), static_cast<float>(fbh),
                                          static_cast<float>(fbw), static_cast<float>(fbh),
                                          tsec, /*clear*/true);
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
        // ADR-0017: seed the undo baseline at the end of the FIRST tick, once the graph has laid out
        // (a pre-loop baseline would capture pre-layout node positions). Then the end-of-frame audit
        // (a no-op unless built with VIVID_UNDO_AUDIT) checks nothing bypassed the gateway.
        if (app.edit_gateway) {
            if (!undo_baseline_seeded) {
                app.edit_gateway->reset_baseline(); undo_baseline_seeded = true;
                // ADR-0018: a recovered autosave is the baseline but differs from disk — start dirty.
                if (app.recovered_unsaved) { app.edit_gateway->mark_dirty(); app.recovered_unsaved = false; }
            } else if (app.reseed_undo_baseline) {
                // A document load/new landed: re-seed the baseline so the freshly opened project is
                // undo entry 0 and the prior project's history is gone (ADR-0017; UX Phase-2 F1 — undo
                // could otherwise reach across a load and clobber the opened project). LOCK the baseline
                // only once the project has fully settled: no async plugin (CLAP) loads are still in
                // flight AND the projection is stable frame-to-frame. Until then a track whose
                // instrument is mid-load projects as a bare audio lane, so a baseline captured then
                // would drop the instrument on undo. `reseed_baseline_if_settling()` keeps the baseline
                // tracking the current doc meanwhile (can_undo stays false, so no undo can hit it). The
                // large frame cap is a safety net against a plugin that never resolves.
                const bool loads_done = !app.session || session_plugin_loads_pending(app.session) == 0;
                const bool stable = app.edit_gateway->reseed_baseline_if_settling();
                if ((loads_done && stable) || ++reseed_settle_frames > 1800) {
                    app.reseed_undo_baseline = false; reseed_settle_frames = 0;
                }
            }
            // Watchdog: recover a gesture group leaked by a lost release (release delivered to another
            // window / consumed by an early handler). Safe — a normal drag holds the button down.
            if (!win.mouse_left_down) app.edit_gateway->close_open_group();
            app.edit_gateway->commit_frame();   // take the frame's deferred snapshot, post-draw/settle
            app.edit_gateway->end_frame_audit();
            // G4: keep the Edit > Undo/Redo titles + enabled state in sync with the history.
            if (app.edit_gateway->revision() != last_undo_rev) {
                last_undo_rev = app.edit_gateway->revision();
                vivid::platform::set_edit_labels(app.edit_gateway->undo_label(), app.edit_gateway->redo_label(),
                                                 app.edit_gateway->can_undo(), app.edit_gateway->can_redo());
            }
            // ADR-0018: mirror the app-level dirty flag onto the macOS window edited-dot when it flips.
            const int cur_dirty = app.edit_gateway->dirty() ? 1 : 0;
            if (cur_dirty != last_dirty) {
                last_dirty = cur_dirty;
                vivid::platform::set_document_edited(cur_dirty != 0);
            }
            // Show the current project's name in the window title (like a document-based app). The
            // macOS edited-dot (above) carries the dirty state, so the title stays just the name.
            {
                std::string title = "Vivid";
                const std::string& pp = app.project.current_project_path;
                if (pp.empty()) {
                    title += " \xE2\x80\x94 Untitled";
                } else {
                    const std::filesystem::path p(pp);
                    title += " \xE2\x80\x94 " +
                             (vivid::project_paths::is_folder_project(pp) ? p.filename().string()
                                                                          : p.stem().string());
                }
                if (title != last_title) { last_title = title; glfwSetWindowTitle(window, title.c_str()); }
            }
            // ADR-0018: periodic autosave — every kAutosaveSecs while the document is dirty, so a
            // crash or kill loses at most that interval. Cleared on a real save (file_actions).
            constexpr double kAutosaveSecs = 15.0;
            const double now = glfwGetTime();
            if (cur_dirty && now - last_autosave >= kAutosaveSecs) {
                last_autosave = now;
                vivid::autosave::write(app, win.win_w, win.win_h, win.split_x, win.dock_h,
                                       static_cast<long long>(std::time(nullptr)));
            }
            // ADR-0018: refresh the warm crash-attribution snapshot (node id ↔ operator type) so a
            // crash in an operator's process_*() can be pinned to a specific node on the next launch.
            if (app.crash_recovery && now - last_snapshot >= 3.0) {
                last_snapshot = now;
                app.crash_recovery->write_snapshot(app);
            }
        }
        return true;
    };
    run_platform_frame_loop(poll_events, tick);
    if (win.editor_win) { win.editor_win->close(app); delete win.editor_win; win.editor_win = nullptr; }  // UI-5 teardown
}

}  // namespace vivid

// ADR-0023 6d: the audio-graph drag continuations, owned by the editor (see input_graph.cpp for the
// press/release/scroll methods). Exactly one gesture is live at a time; each routes edits through the
// session C-API + the EditGateway, exactly as the free-function continuation did.
namespace vivid::ui {
namespace S = vivid::session;

void AudioNodeGraph::on_move(App& app, Window& win, double mx, double my) {
    // 2i: drag empty space to pan the absolute camera (scale unchanged).
    if (panning) {   // ADR-0023 #3d: shared incremental pan on the canvas-owned camera
        canvas_.pan(static_cast<float>(mx - pan_last_mx), static_cast<float>(my - pan_last_my));
        pan_last_mx = mx; pan_last_my = my;
        view_init_ = true;
    }
    // UI-3 Stage 1: drag a selected node's param knob (vertical) or a pinned slider row (horizontal).
    if (param_drag >= 0 && app.session && win.sel_audio_node >= 0) {
        const int tr = std::min(std::max(win.sel_track, 0), S::session_track_count(app.session) - 1);
        const int nid = win.sel_audio_node;   // node id (not chain index)
        const float mn = S::session_audio_graph_node_param_min(app.session, tr, nid, param_drag);
        const float mxx = S::session_audio_graph_node_param_max(app.session, tr, nid, param_drag);
        const float norm = param_horiz
            ? std::clamp(static_cast<float>(mx - param_rx) / param_rw, 0.f, 1.f)
            : std::clamp(param_v0 + static_cast<float>(param_y0 - my) * 0.006f, 0.f, 1.f);
        S::session_audio_graph_node_param_set(app.session, tr, nid, param_drag, mn + norm * (mxx - mn));
        if (app.edit_gateway) app.edit_gateway->note_edit("Set Param", "ag-param-drag");   // ADR-0017/G3
    }
    // Reposition drag: the grabbed node follows the cursor (screen -> world via the shared transform).
    // A drag in progress (node_drag >= 0) continues regardless of dock focus — the pane is always live.
    if (node_drag >= 0 && app.session) {
        const int tr = std::min(std::max(win.sel_track, 0), S::session_track_count(app.session) - 1);
        prime(app, win);   // node-canvas bounds (below-session pane) + param bounds + selection
        double wxd, wyd; canvas_.view().to_world(mx, my, wxd, wyd);   // screen -> world via the absolute camera
        // ADR-0033 P1 group-drag: the grabbed node follows the cursor; every other selected node shifts
        // by the same world delta (measured from the grabbed node's grab-time position).
        const float tgt_x = static_cast<float>(wxd) - node_dx, tgt_y = static_cast<float>(wyd) - node_dy;
        float start_x = tgt_x, start_y = tgt_y;
        for (const auto& e : grp_start_) if (e.first == node_drag) { start_x = e.second.first; start_y = e.second.second; break; }
        const float ddx = tgt_x - start_x, ddy = tgt_y - start_y;
        if (grp_start_.empty()) {
            S::session_audio_graph_node_set_pos(app.session, tr, node_drag, tgt_x, tgt_y);
        } else {
            for (const auto& e : grp_start_)
                S::session_audio_graph_node_set_pos(app.session, tr, e.first, e.second.first + ddx, e.second.second + ddy);
        }
        if (app.edit_gateway) app.edit_gateway->note_edit("Move Node", "ag-node-drag");   // ADR-0017/G3
    }
    if (marquee_ && app.session) {   // ADR-0033 P1: extend the marquee's far corner (world coords)
        double wxd, wyd; canvas_.view().to_world(mx, my, wxd, wyd);
        marq_x1_ = wxd; marq_y1_ = wyd;
    }
    // Drag a source node's key-range handle (vertical): ~0.25 semitone/px, lo/hi kept ordered.
    if (key_drag >= 0 && app.session && win.sel_audio_node >= 0) {
        const int tr = std::min(std::max(win.sel_track, 0), S::session_track_count(app.session) - 1);
        int lo = 0, hi = 127;
        S::session_audio_graph_node_key_range_get(app.session, tr, win.sel_audio_node, &lo, &hi);
        const int nv = std::clamp(key_v0 + static_cast<int>((key_y0 - my) * 0.25), 0, 127);
        if (key_drag == 0) lo = std::min(nv, hi); else hi = std::max(nv, lo);
        S::session_audio_graph_node_key_range_set(app.session, tr, win.sel_audio_node, lo, hi);
        if (app.edit_gateway) app.edit_gateway->note_edit("Set Key Range", "ag-key-drag");   // ADR-0017/G3
    }
}

}  // namespace vivid::ui
