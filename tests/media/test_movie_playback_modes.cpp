// Tests for play-mode behavior and source-change queue mechanics.
// Covers validation matrix gaps: loop/once/hold-last modes, source change
// during worker activity, and budget reset on source change.

#include "movie_transport.h"
#include "playback_session.h"
#include "session_registry.h"
#include "video_decode_worker.h"
#include "test_helpers.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

static constexpr double kEps = 1e-9;

// ============================================================================
// Play mode tests
// ============================================================================

static void test_loop_mode_time_wraps() {
    MovieTransport t;
    t.set_source(10.0);
    t.set_play_mode(PlayMode::Loop);

    // Audio time exceeding duration should wrap
    check(std::abs(t.compute_audio_master_time(12.0f, 0.0) - 2.0) < kEps,
          "loop: 12.0 wraps to 2.0 with 10s duration");
    check(std::abs(t.compute_audio_master_time(25.5f, 0.0) - 5.5) < kEps,
          "loop: 25.5 wraps to 5.5 with 10s duration");
    check(std::abs(t.compute_audio_master_time(10.0f, 0.0) - 0.0) < kEps,
          "loop: 10.0 wraps to 0.0 with 10s duration");

    // Sequence crossing the loop boundary
    double prev = -1.0;
    bool all_in_range = true;
    for (int i = 0; i < 30; ++i) {
        float audio_t = static_cast<float>(i) * 0.5f; // 0, 0.5, 1.0, ..., 14.5
        double local = t.compute_audio_master_time(audio_t, 0.0);
        if (local < 0.0 || local >= 10.0) { all_in_range = false; break; }
        prev = local;
    }
    check(all_in_range, "loop: all wrapped times in [0, duration)");
}

static void test_once_mode_behavior() {
    MovieTransport t;
    t.set_source(10.0);
    t.set_play_mode(PlayMode::Once);

    // Once mode in self-clock: time at or past duration should NOT clamp
    // (Once mode stopping is handled by the decoder, not the transport).
    // Only HoldLast clamps in compute_self_clock_time.
    double result = t.compute_self_clock_time(15.0);
    check(result == 15.0, "once: self_clock does not clamp past duration");

    // Audio-master still wraps (wrapping is duration-based, not mode-based)
    double wrapped = t.compute_audio_master_time(12.0f, 0.0);
    check(std::abs(wrapped - 2.0) < kEps, "once: audio_master still wraps");
}

static void test_hold_last_sequence() {
    MovieTransport t;
    t.set_source(10.0);
    t.set_play_mode(PlayMode::HoldLast);

    // Feed 20 ticks with advancing time
    bool all_clamped = true;
    for (int i = 0; i < 20; ++i) {
        double decoder_t = static_cast<double>(i) * 0.7; // 0, 0.7, ..., 13.3
        double result = t.compute_self_clock_time(decoder_t);
        if (result > 10.0 + kEps) { all_clamped = false; break; }
        if (decoder_t <= 10.0) {
            if (std::abs(result - decoder_t) > kEps) { all_clamped = false; break; }
        } else {
            if (std::abs(result - 10.0) > kEps) { all_clamped = false; break; }
        }
    }
    check(all_clamped, "hold_last: all times clamped at duration");
}

// ============================================================================
// Source change tests
// ============================================================================

static void test_source_change_resets_all() {
    MovieTransport t;
    t.set_source(10.0);
    t.set_frame_rate(30.0f);
    uint64_t gen1 = t.source_generation();

    // Consume source-change seek
    auto d0 = t.evaluate_correction(0.0, 0.0, 0.0, 0.0);
    check(d0.type == CorrectionType::Seek, "source_change: first eval is Seek");
    t.record_seek_issued(0.0);

    // Change source
    t.set_source(20.0);
    check(t.source_generation() > gen1, "source_change: generation incremented");
    check(std::abs(t.duration() - 20.0) < kEps, "source_change: duration updated");

    // First correction after source change should be Seek
    auto d1 = t.evaluate_correction(5.0, 5.0, 5.0, 1.0);
    check(d1.type == CorrectionType::Seek, "source_change: first eval after change is Seek");
}

static void test_source_change_during_worker_flushes() {
    VideoDecodeWorker w;
    w.start();

    // Submit a slow work item
    w.submit_work([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        DecodedFrame f;
        f.width = 320;
        f.height = 240;
        f.data.resize(320 * 240 * 4, 0xAA);
        f.pts = 1.0;
        return f;
    });

    // Flush immediately (simulates source change)
    w.flush();

    // Queue should be empty after flush
    DecodedFrame out;
    check(!w.pop_latest(out), "worker_flush: queue empty after flush");

    // Submit a new work item and verify it completes
    w.submit_work([]() {
        DecodedFrame f;
        f.width = 160;
        f.height = 120;
        f.data.resize(160 * 120 * 4, 0xBB);
        f.pts = 2.0;
        return f;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check(w.pop_latest(out), "worker_flush: recovery after flush");
    check(out.pts == 2.0, "worker_flush: correct frame after recovery");

    w.stop();
}

static void test_source_change_resets_exhausted_budget() {
    MovieTransport t;
    t.set_source(10.0);
    t.set_frame_rate(30.0f);

    // Consume source-change seek + exhaust budget
    auto d0 = t.evaluate_correction(0.0, 0.0, 0.0, 0.0);
    t.record_seek_issued(0.0);
    for (int i = 1; i <= 3; ++i) {
        double mono = 0.2 * i;
        auto d = t.evaluate_correction(5.3, 5.0, mono, mono);
        t.record_seek_issued(mono);
    }

    // Budget exhausted
    auto d_exhausted = t.evaluate_correction(5.3, 5.0, 1.5, 0.95);
    check(d_exhausted.type == CorrectionType::DropRepeat, "budget_reset: exhausted before source change");

    // Source change resets budget
    t.set_source(20.0);
    auto d_after = t.evaluate_correction(5.3, 5.0, 2.0, 2.0);
    check(d_after.type == CorrectionType::Seek, "budget_reset: Seek after source change");
}

static void test_same_path_sessions_stay_independent() {
    auto& reg = PlaybackSessionRegistry::instance();
    auto video = reg.acquire("movie_video", "/tmp/shared.mp4");
    auto audio = reg.acquire("movie_audio", "/tmp/shared.mp4");

    check(video.get() != audio.get(), "session_isolation: same path yields distinct sessions");

    video->transport().set_source(10.0);
    audio->transport().set_source(10.0);
    video->transport().set_speed(0.5f);
    audio->transport().set_speed(1.5f);
    video->transport().set_play_mode(PlayMode::HoldLast);
    audio->transport().set_play_mode(PlayMode::Loop);

    check(std::abs(video->transport().speed() - 0.5f) < kEps,
          "session_isolation: video speed remains independent");
    check(std::abs(audio->transport().speed() - 1.5f) < kEps,
          "session_isolation: audio speed remains independent");
    check(video->transport().play_mode() == PlayMode::HoldLast,
          "session_isolation: video play mode remains independent");
    check(audio->transport().play_mode() == PlayMode::Loop,
          "session_isolation: audio play mode remains independent");

    reg.release("movie_video");
    reg.release("movie_audio");
}

// ============================================================================

int main() {
    std::fprintf(stderr, "=== Play Mode Tests ===\n");
    test_loop_mode_time_wraps();
    test_once_mode_behavior();
    test_hold_last_sequence();

    std::fprintf(stderr, "\n=== Source Change Tests ===\n");
    test_source_change_resets_all();
    test_source_change_during_worker_flushes();
    test_source_change_resets_exhausted_budget();
    test_same_path_sessions_stay_independent();

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Playback mode tests: %d failed\n", failures);
    std::fprintf(stderr, "========================================\n");
    return failures == 0 ? 0 : 1;
}
