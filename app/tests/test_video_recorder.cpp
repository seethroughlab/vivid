// Headless test for the realtime AV export coordinator (app/video_recorder). A MockAVExporter
// substitutes for AVFoundation, and a real Transport (header-only) feeds the lock-free recording
// tap — so this runs anywhere, no GPU/AVFoundation. Asserts: even-dimension locking, per-frame
// video forwarding, audio drain, blank-frame skip, mid-record crop, single finish, deadline
// auto-stop, and the RecordingTap ring's overrun behavior.

#include "app/video_recorder.h"
#include "platform/av_exporter.h"
#include "transport.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace vivid;

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

// A recording-capable stand-in for AVFExporter. Records what the coordinator forwarded.
struct MockAVExporter : public AVExporter {
    bool     started = false;
    bool     finished = false;
    bool     recording = false;
    uint32_t start_w = 0, start_h = 0, start_sr = 0;
    double   start_fps = 0.0;
    uint64_t video_frames = 0;
    uint32_t last_frame_w = 0, last_frame_h = 0;
    uint64_t audio_samples = 0;   // total interleaved samples forwarded
    double   fake_elapsed = 0.0;  // drives the deadline check
    std::string path_;

    bool start(const std::string& p, uint32_t w, uint32_t h, double fps, uint32_t sr) override {
        started = true; recording = true;
        path_ = p; start_w = w; start_h = h; start_fps = fps; start_sr = sr;
        return true;
    }
    bool write_video_frame(const uint8_t*, uint32_t w, uint32_t h) override {
        if (!recording) return false;
        ++video_frames; last_frame_w = w; last_frame_h = h; return true;
    }
    bool write_audio_samples(const float*, uint64_t n, uint32_t) override {
        if (!recording) return false;
        audio_samples += n; return true;
    }
    bool finish() override { finished = true; recording = false; return true; }
    bool is_recording() const override { return recording; }
    const std::string& output_path() const override { return path_; }
    uint64_t frame_count() const override { return video_frames; }
    double fps() const override { return start_fps; }
    double elapsed_sec() const override { return fake_elapsed; }
};

// The coordinator's default ctor references this; the tests only use the injecting ctor, so it is
// never actually called — but the symbol must resolve to link video_recorder.cpp without AVFoundation.
namespace vivid {
std::unique_ptr<AVExporter> make_platform_av_exporter() { return std::make_unique<MockAVExporter>(); }
}

// Build a recorder around a mock we retain a raw pointer to for inspection.
static VideoRecorder make_rec(MockAVExporter*& out) {
    auto mock = std::make_unique<MockAVExporter>();
    out = mock.get();
    return VideoRecorder(std::move(mock));
}

static std::vector<uint8_t> frame_buf(uint32_t w, uint32_t h) {
    return std::vector<uint8_t>(static_cast<size_t>(w) * h * 4, 0x40);
}

int main() {
    // 1. Even-dimension locking + basic video/audio forwarding.
    {
        MockAVExporter* m = nullptr;
        VideoRecorder rec = make_rec(m);
        Transport tr;
        std::string err;
        // Odd dims 101×99 must floor to 100×98.
        CHECK(rec.start("/tmp/out.mp4", 60.0, 0.0, 101, 99, 48000, tr, &err));
        CHECK(m->started);
        CHECK(m->start_w == 100 && m->start_h == 98);
        CHECK(m->start_sr == 48000);
        CHECK(rec.is_recording());

        // Feed 3 frames matching the locked size; each tick also drains whatever audio is queued.
        auto fb = frame_buf(100, 98);
        for (int i = 0; i < 3; ++i) {
            std::vector<float> block(256 * 2, 0.1f);   // 256 stereo frames
            tr.recording_tap_write(block.data(), 256);
            CHECK(rec.tick(fb.data(), 100, 98, tr) == false);   // no deadline
        }
        CHECK(m->video_frames == 3);
        CHECK(m->last_frame_w == 100 && m->last_frame_h == 98);
        CHECK(m->audio_samples == 3 * 256 * 2);

        // 2. Blank frame → no video appended, audio still drained.
        std::vector<float> block(128 * 2, 0.2f);
        tr.recording_tap_write(block.data(), 128);
        rec.tick(nullptr, 0, 0, tr);
        CHECK(m->video_frames == 3);                 // unchanged
        CHECK(m->audio_samples == 3 * 256 * 2 + 128 * 2);

        // Stop finalizes exactly once; status reflects the finished export.
        auto st = rec.stop();
        CHECK(m->finished);
        CHECK(!rec.is_recording());
        CHECK(st.recording == false);
        CHECK(st.frames == 3);
        CHECK(st.width == 100 && st.height == 98);
        // A second stop is a no-op (no double finish semantics to worry about).
        rec.stop();
    }

    // 3. Mid-record crop: source larger than the locked size still writes the locked size.
    {
        MockAVExporter* m = nullptr;
        VideoRecorder rec = make_rec(m);
        Transport tr;
        CHECK(rec.start("/tmp/out.mov", 30.0, 0.0, 100, 100, 44100, tr, nullptr));
        auto big = frame_buf(102, 100);   // 2px wider than locked
        rec.tick(big.data(), 102, 100, tr);
        CHECK(m->video_frames == 1);
        CHECK(m->last_frame_w == 100 && m->last_frame_h == 100);
        // Source smaller than locked → skipped.
        auto small = frame_buf(80, 80);
        rec.tick(small.data(), 80, 80, tr);
        CHECK(m->video_frames == 1);   // unchanged
        rec.stop();
    }

    // 4. Deadline auto-stop: once elapsed passes `seconds`, tick() stops itself and returns true.
    {
        MockAVExporter* m = nullptr;
        VideoRecorder rec = make_rec(m);
        Transport tr;
        CHECK(rec.start("/tmp/timed.mp4", 60.0, 3.0, 64, 64, 48000, tr, nullptr));
        auto fb = frame_buf(64, 64);
        m->fake_elapsed = 1.0;
        CHECK(rec.tick(fb.data(), 64, 64, tr) == false);   // before deadline
        CHECK(rec.is_recording());
        m->fake_elapsed = 3.5;                              // past the 3s deadline
        CHECK(rec.tick(fb.data(), 64, 64, tr) == true);     // auto-stopped this tick
        CHECK(m->finished);
        CHECK(!rec.is_recording());
    }

    // 5. Bad-path validation is rejected before the exporter starts.
    {
        MockAVExporter* m = nullptr;
        VideoRecorder rec = make_rec(m);
        Transport tr;
        std::string err;
        CHECK(!rec.start("relative.mp4", 60.0, 0.0, 64, 64, 48000, tr, &err));      // not absolute
        CHECK(!rec.start("/tmp/../etc/x.mp4", 60.0, 0.0, 64, 64, 48000, tr, &err)); // has ..
        CHECK(!rec.start("/tmp/x.gif", 60.0, 0.0, 64, 64, 48000, tr, &err));        // bad ext
        CHECK(!rec.start("/tmp/x.mp4", 60.0, 0.0, 0, 0, 48000, tr, &err));          // empty output
        CHECK(!m->started);
    }

    // 6. RecordingTap ring: monotonic drain + overrun drop.
    {
        Transport tr;
        tr.start_recording_tap();
        CHECK(tr.available_recorded_samples() == 0);
        std::vector<float> block(1000 * 2, 0.5f);
        tr.recording_tap_write(block.data(), 1000);
        CHECK(tr.available_recorded_samples() == 1000 * 2);
        std::vector<float> out(4096);
        uint64_t got = tr.pop_recorded_samples(out.data(), out.size());
        CHECK(got == 1000 * 2);
        CHECK(tr.available_recorded_samples() == 0);
        tr.stop_recording_tap();
    }

    if (g_failures == 0) std::fprintf(stderr, "test_video_recorder: OK\n");
    return g_failures;
}
