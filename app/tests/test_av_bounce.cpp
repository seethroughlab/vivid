// ADR-0032 Phase C: headless coverage of the PURE offline AV loop (av_bounce_run). Drives a real
// native-op Session through the loop with a MOCK frame source + MOCK exporter (no GPU, no AVFoundation),
// asserting the load-bearing determinism: video frame count, sample-accurate audio length, video PTS on
// the i/fps grid, audio PTS at the exact sample position, and the synthetic beat clock — including the
// awkward 44100/60 = 735 case where samples-per-frame isn't integer.
//
// macOS-only (the audio engine reaches CoreFoundation via the VST3 host), full-app configure.
#include "app/av_bounce.h"
#include "platform/av_exporter.h"
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"   // register_builtin_audio_ops
#include "gpu/op_runtime.h"            // vivid::OpRegistry
#include "midi/midi_clip.h"            // ClipNote
#include "transport.h"
#include "test_helpers.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace vivid::session;

// Records the (time, beats) sequence and hands back a fixed RGBA buffer of its declared size.
struct MockFrameSource : vivid::AvFrameSource {
    uint32_t w, h;
    std::vector<double> times, beats;
    MockFrameSource(uint32_t W, uint32_t H) : w(W), h(H) {}
    uint32_t width()  const override { return w; }
    uint32_t height() const override { return h; }
    bool render(double time_sec, double beats_at, std::vector<uint8_t>& rgba) override {
        times.push_back(time_sec); beats.push_back(beats_at);
        rgba.assign(static_cast<size_t>(w) * h * 4, 0x40);
        return true;
    }
};

// Records the PTS + counts the loop forwards.
struct MockAV : vivid::AVExporter {
    bool started = false, finished = false, offline = false;
    double sfps = 0.0;
    std::vector<double> vpts, apts;
    uint64_t vframes = 0, asamples = 0;
    std::string path_;
    void begin_offline() override { offline = true; }
    bool start(const std::string& p, uint32_t, uint32_t, double fps, uint32_t) override {
        started = true; path_ = p; sfps = fps; return true;
    }
    bool write_video_frame(const uint8_t*, uint32_t, uint32_t, double pts = -1.0) override {
        ++vframes; vpts.push_back(pts); return true;
    }
    bool write_audio_samples(const float*, uint64_t n, uint32_t, double pts = -1.0) override {
        asamples += n; apts.push_back(pts); return true;
    }
    bool finish() override { finished = true; return true; }
    bool is_recording() const override { return started && !finished; }
    const std::string& output_path() const override { return path_; }
    uint64_t frame_count() const override { return vframes; }
    double fps() const override { return sfps; }
    double elapsed_sec() const override { return 0.0; }
};

static Session* make_tone_session(vivid::OpRegistry& reg, uint32_t sr) {
    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);
    const int t = session_add_graph_track(s, "T");
    session_set_track_audio_instrument(s, t, "TestTone");
    ClipNote n{}; n.pitch = 60; n.start = 0.0; n.dur = 100.0; n.vel = 0.9f;
    session_set_clip(s, t, 0, &n, 1, 100.0);
    session_launch_scene(s, 0);
    return s;
}

static void run_case(vivid::OpRegistry& reg, uint32_t sr, double fps, double seconds, double bpm) {
    Transport tr; tr.configure_capture(sr); tr.bpm.store(bpm);
    Session* s = make_tone_session(reg, sr);
    MockFrameSource src(64, 48);
    MockAV ex;
    vivid::AvBounceRequest req; req.path = "/tmp/av.mp4"; req.seconds = seconds; req.fps = fps;
    vivid::AvBounceResult res; std::string err;
    CHECK(vivid::av_bounce_run(s, tr, src, ex, /*feed*/ nullptr, req, res, &err));

    const uint64_t total_frames = static_cast<uint64_t>(std::llround(seconds * fps));
    CHECK(res.frames == total_frames);
    CHECK(ex.vframes == total_frames);
    CHECK(src.times.size() == total_frames);

    // Audio is sample-accurate: Σ audio frames telescopes to llround(total_frames * sr/fps), no drift.
    const uint64_t expected_audio = static_cast<uint64_t>(std::llround(static_cast<double>(total_frames) * sr / fps));
    CHECK(res.audio_frames == expected_audio);
    CHECK(ex.asamples == expected_audio * 2);   // interleaved stereo

    // Video PTS on the even i/fps grid; audio PTS starts at 0 and strictly increases.
    for (uint64_t i = 0; i < total_frames; ++i) CHECK_NEAR(ex.vpts[i], static_cast<double>(i) / fps, 1e-9);
    CHECK(ex.apts.front() == 0.0);
    for (size_t i = 1; i < ex.apts.size(); ++i) CHECK(ex.apts[i] > ex.apts[i - 1]);

    // Synthetic beat clock: beats == time * bpm/60; first frame at beat 0.
    CHECK(src.beats.front() == 0.0);
    for (uint64_t i = 0; i < total_frames; ++i) CHECK_NEAR(src.beats[i], src.times[i] * bpm / 60.0, 1e-9);
    // Audio and video share one clock: audio PTS == the frame's visual time.
    for (uint64_t i = 0; i < total_frames; ++i) CHECK_NEAR(ex.apts[i], src.times[i], 1e-9);

    session_destroy(s);
}

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);

    // The awkward 44100/60 (=735/736 alternation), an even 48000/30, and a coprime 44100/24.
    run_case(reg, 44100, 60.0, 0.5, 120.0);
    run_case(reg, 48000, 30.0, 0.25, 128.0);
    run_case(reg, 44100, 24.0, 1.0, 90.0);

    // Bad requests are rejected before anything is written.
    {
        Transport tr; tr.configure_capture(48000); tr.bpm.store(120.0);
        Session* s = make_tone_session(reg, 48000);
        MockFrameSource src(64, 48); MockAV ex;
        vivid::AvBounceResult r; std::string e;
        vivid::AvBounceRequest nodur; nodur.path = "/tmp/av.mp4"; nodur.seconds = 0.0; nodur.fps = 60.0;
        CHECK(!vivid::av_bounce_run(s, tr, src, ex, nullptr, nodur, r, &e));        // no duration
        CHECK(!vivid::av_bounce_run(nullptr, tr, src, ex, nullptr, nodur, r, &e));  // no session
        CHECK(ex.vframes == 0);
        session_destroy(s);
    }

    return vivid::test::summary("test_av_bounce");
}
