#include "app/av_bounce.h"

#include "app/app.h"
#include "app/window.h"             // ADR-0032 Phase C2: Window (reactive-feed smoothing state)
#include "app/frame.h"             // ADR-0032 Phase C2: publish_bridge_sources / apply_audio_param_mappings
#include "audio/vst3_host.h"        // vivid::session::session_process (all-notes-off flush)
#include "transport.h"
#include "gpu/visual_graph.h"       // VisualGraph + WGPU types (run_chain / read_output_pixels)
#include "platform/av_exporter.h"

#include "miniaudio.h"              // ma_device_stop / ma_device_start

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// The GPU + device orchestration for the deterministic offline AV export — the App-layer half that
// av_bounce.cpp's pure loop can't include (it would drag the whole GPU/AVFoundation stack into the
// AUDIO_ENGINE test tier). Mirrors audio_bounce_app.cpp.
namespace vivid {

namespace {

// Absolute, no "..", .mp4/.mov — mirrors audio_bounce.cpp's is_safe_wav_path + video_recorder's guard.
bool is_safe_av_path(const std::string& p, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (p.empty())   return fail("path is empty");
    if (p[0] != '/') return fail("path must be absolute");
    if (p.find("..") != std::string::npos) return fail("path must not contain '..'");
    auto dot = p.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? "" : p.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext != "mp4" && ext != "mov") return fail("path must end in .mp4 or .mov");
    return true;
}

// ADR-0032 Phase C2: replicate audio_callback.cpp:82-114's master metering over the offline PCM so the
// exported reactive visuals look IDENTICAL to what the artist saw live for the same audio. The one-pole
// crossover + transient-baseline state carry across frames, starting fresh (matching a render from beat 0).
struct OfflineMeter {
    float m_flt_lo = 0.f, m_flt_hi = 0.f, tr_baseline = 0.f;
    void advance(const float* pcm, uint32_t frames, uint32_t sr, Transport& tr) {
        if (frames == 0 || sr == 0) return;
        const float a_lo = 1.f - std::exp(-6.2832f * 200.f  / static_cast<float>(sr));
        const float a_hi = 1.f - std::exp(-6.2832f * 2000.f / static_cast<float>(sr));
        double sum_sq = 0.0, slo = 0.0, smi = 0.0, shi = 0.0;
        for (uint32_t i = 0; i < frames; ++i) {
            const float l = pcm[i * 2];   // left channel, exactly like the callback
            sum_sq += static_cast<double>(l) * l;
            m_flt_lo += (l - m_flt_lo) * a_lo;
            m_flt_hi += (l - m_flt_hi) * a_hi;
            const float lo = m_flt_lo, mi = m_flt_hi - m_flt_lo, hi = l - m_flt_hi;
            slo += static_cast<double>(lo) * lo; smi += static_cast<double>(mi) * mi; shi += static_cast<double>(hi) * hi;
        }
        const double inv = 1.0 / frames;
        const float rms = static_cast<float>(std::sqrt(sum_sq * inv));
        constexpr auto rel = std::memory_order_relaxed;
        tr.level.store(rms, rel);
        tr.band_low.store(static_cast<float>(std::sqrt(slo * inv)), rel);
        tr.band_mid.store(static_cast<float>(std::sqrt(smi * inv)), rel);
        tr.band_high.store(static_cast<float>(std::sqrt(shi * inv)), rel);
        const float t = std::max(0.0f, (rms - tr_baseline) * 6.0f);
        tr_baseline += (rms - tr_baseline) * 0.04f;
        tr.transient.store(std::min(1.0f, t), rel);
    }
};

// Per-frame reactive feed: compute the master meter from THIS frame's PCM, publish it + the synthetic
// beats into the bridge (reusing the live publish_bridge_sources), so the export's run_chain reacts to the
// audio being muxed. Device is stopped + the main thread is stepping the job, so writing the transport
// atomics + advancing the live Window's smoothing state is race-free and invisible to the (paused) UI.
struct OfflineReactiveFeed : AvReactiveFeed {
    App&       app;
    Window&    win;
    Transport& tr;
    uint32_t   sr;
    OfflineMeter meter;
    OfflineReactiveFeed(App& a, Window& w, Transport& t, uint32_t r) : app(a), win(w), tr(t), sr(r) {}
    void feed(const float* pcm, uint32_t frames, double beats) override {
        meter.advance(pcm, frames, sr, tr);
        tr.beats.store(beats, std::memory_order_relaxed);
        publish_bridge_sources(app, win);      // master scalars/transport sources + session FFT -> bridge
        apply_audio_param_mappings(app);        // drive mapped visual params from the freshly-published bridge
    }
};

// The production frame source: advance app.vgraph to (time, beats) and read back its Output RT.
struct VisualGraphFrameSource : AvFrameSource {
    VisualGraph& vg;
    float bpm;
    int   bpb;
    VisualGraphFrameSource(VisualGraph& v, float b, int p) : vg(v), bpm(b), bpb(p) {}

    uint32_t width()  const override { return vg.rt_w() & ~1u; }   // even for H.264
    uint32_t height() const override { return vg.rt_h() & ~1u; }

    bool render(double time_sec, double beats, std::vector<uint8_t>& rgba) override {
        vg.set_metronome(bpm, bpb, beats);
        // Own the render encoder + submit it BEFORE read_output_pixels (which only submits its own copy
        // encoder). No swapchain / begin_frame — a headless offscreen render.
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(vg.device(), nullptr);
        vg.run_chain(enc, static_cast<float>(time_sec));
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(vg.queue(), 1, &cmd);
        wgpuCommandBufferRelease(cmd);
        wgpuCommandEncoderRelease(enc);

        uint32_t rw = 0, rh = 0;
        if (!vg.read_output_pixels(rgba, rw, rh)) return false;
        // Crop to even dims if the RT happens to be odd (H.264 needs even; typical RTs are already even).
        const uint32_t ew = rw & ~1u, eh = rh & ~1u;
        if (ew != rw || eh != rh) {
            for (uint32_t y = 0; y < eh; ++y)
                std::memmove(&rgba[static_cast<size_t>(y) * ew * 4],
                             &rgba[static_cast<size_t>(y) * rw * 4], static_cast<size_t>(ew) * 4);
            rgba.resize(static_cast<size_t>(ew) * eh * 4);
        }
        return true;
    }
};

// The active export job, owned via the opaque App::av_export pointer so app.h stays GPU/miniaudio-free.
struct AvExportRunner {
    std::unique_ptr<AVExporter> exporter;
    std::unique_ptr<VisualGraphFrameSource> src;
    std::unique_ptr<OfflineReactiveFeed> feed;   // Phase C2: null when no Window (headless caller)
    AvExportJob job;
    ma_device* dev = nullptr;
    vivid::session::Session* session = nullptr;
    Transport* tr = nullptr;
    // Phase C2: the live transport reactive state, saved before the render and restored after, so the
    // resumed UI's meters/playhead aren't left showing the export's final frame.
    float sv_level = 0.f, sv_low = 0.f, sv_mid = 0.f, sv_high = 0.f, sv_tr = 0.f; double sv_beats = 0.0;
};

// One output frame per app frame-loop tick: keeps the main loop responsive + shows a live render preview,
// and still exports faster than realtime for light graphs (the app runs well above the export fps).
constexpr uint32_t kFramesPerTick = 1;

}  // namespace

bool av_export_start(App& app, Window* win, const AvBounceRequest& req, std::string* err) {
    if (app.av_export) { if (err) *err = "an AV export is already running"; return false; }
    if (!app.session)   { if (err) *err = "no session";      return false; }
    if (!app.transport) { if (err) *err = "no transport";    return false; }
    if (!app.vgraph)    { if (err) *err = "no visual graph"; return false; }
    if (!is_safe_av_path(req.path, err)) return false;
    Transport&   tr = *app.transport;
    VisualGraph& vg = *app.vgraph;
    const uint32_t w = vg.rt_w() & ~1u, h = vg.rt_h() & ~1u;
    if (w == 0 || h == 0) { if (err) *err = "visual output has zero size"; return false; }
    const uint32_t sr = tr.audio_sample_rate();
    if (sr == 0) { if (err) *err = "audio sample rate unknown (audio device not initialized)"; return false; }
    const double fps = req.fps > 0.0 ? req.fps : 60.0;

    auto r = std::make_unique<AvExportRunner>();
    r->dev = static_cast<ma_device*>(app.audio_device);
    r->session = app.session; r->tr = &tr;
    // Pause the device for the job's lifetime so the RT callback can't race session_process on the shared
    // session (same discipline as run_audio_bounce). Resumed in the finishing tick.
    if (r->dev) ma_device_stop(r->dev);
    r->exporter = make_platform_av_exporter();
    r->exporter->begin_offline();
    if (!r->exporter->start(req.path, w, h, fps, sr)) {
        if (r->dev) ma_device_start(r->dev);
        if (err) *err = "could not start AV export: " + req.path;
        return false;
    }
    r->src = std::make_unique<VisualGraphFrameSource>(
        vg, static_cast<float>(tr.bpm.load(std::memory_order_relaxed)),
        tr.beats_per_bar.load(std::memory_order_relaxed));
    // Phase C2: with a live Window, feed offline reactivity (master meter + synthetic beats) into the
    // bridge. Save the live transport reactive state first (restored when the job finishes).
    AvReactiveFeed* feed_ptr = nullptr;
    if (win) {
        constexpr auto rel = std::memory_order_relaxed;
        r->sv_level = tr.level.load(rel);   r->sv_low  = tr.band_low.load(rel);
        r->sv_mid   = tr.band_mid.load(rel); r->sv_high = tr.band_high.load(rel);
        r->sv_tr    = tr.transient.load(rel); r->sv_beats = tr.beats.load(rel);
        r->feed = std::make_unique<OfflineReactiveFeed>(app, *win, tr, sr);
        feed_ptr = r->feed.get();
    }
    if (!r->job.begin(app.session, tr, *r->src, *r->exporter, feed_ptr, req, err)) {
        r->exporter->finish();
        if (r->dev) ma_device_start(r->dev);
        return false;
    }
    app.av_export = r.release();
    return true;
}

AvTick av_export_tick(App& app) {
    if (!app.av_export) return AvTick::Idle;
    auto* r = static_cast<AvExportRunner*>(app.av_export);
    if (r->job.step(kFramesPerTick)) return AvTick::Running;

    // Finished this tick: finalize the file, store the result, all-notes-off at the live beat (voices
    // advanced from beat 0), resume the device, free the job.
    r->exporter->finish();
    app.last_av_export = r->job.result();
    const uint32_t sr = r->tr->audio_sample_rate();
    if (sr) {
        std::vector<float> flush(1024 * 2, 0.f);
        vivid::session::session_process(r->session, flush.data(), 1024, sr,
                                        r->tr->bpm.load(std::memory_order_relaxed),
                                        r->tr->beats.load(std::memory_order_relaxed),
                                        r->tr->beats_per_bar.load(std::memory_order_relaxed),
                                        /*playing*/false, /*release_all*/true);
    }
    // Phase C2: restore the live transport reactive state the feed overwrote (so the resumed UI is clean).
    if (r->feed) {
        constexpr auto rel = std::memory_order_relaxed;
        r->tr->level.store(r->sv_level, rel);   r->tr->band_low.store(r->sv_low, rel);
        r->tr->band_mid.store(r->sv_mid, rel);  r->tr->band_high.store(r->sv_high, rel);
        r->tr->transient.store(r->sv_tr, rel);  r->tr->beats.store(r->sv_beats, rel);
    }
    if (r->dev) ma_device_start(r->dev);
    delete r;
    app.av_export = nullptr;
    return AvTick::Done;
}

bool av_export_active(const App& app) { return app.av_export != nullptr; }

bool av_export_progress(const App& app, uint64_t& frames_done, uint64_t& total_frames) {
    if (!app.av_export) return false;
    auto* r = static_cast<AvExportRunner*>(app.av_export);
    frames_done  = r->job.frames_done();
    total_frames = r->job.total_frames();
    return true;
}

}  // namespace vivid
