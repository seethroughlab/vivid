// Unit tests for the movie_session shared library:
// MovieTransport, PlaybackSession, PlaybackSessionRegistry.

#include "movie_transport.h"
#include "playback_session.h"
#include "session_registry.h"
#include "test_helpers.h"

#include <cmath>
#include <cstdio>

static constexpr double kEps = 1e-9;

// ============================================================================
// wrap_time tests (same cases as test_movie_av_sync.cpp)
// ============================================================================

static void test_wrap_time() {
    check(std::abs(wrap_time(0.0, 10.0) - 0.0) < kEps, "wrap_time(0, 10) == 0");
    check(std::abs(wrap_time(5.0, 10.0) - 5.0) < kEps, "wrap_time(5, 10) == 5");
    check(std::abs(wrap_time(10.0, 10.0) - 0.0) < kEps, "wrap_time(10, 10) == 0");
    check(std::abs(wrap_time(15.5, 10.0) - 5.5) < kEps, "wrap_time(15.5, 10) == 5.5");
    check(std::abs(wrap_time(-1.0, 10.0) - 9.0) < kEps, "wrap_time(-1, 10) == 9");
    check(std::abs(wrap_time(5.0, 0.0) - 5.0) < kEps, "wrap_time(5, 0) == 5");
    check(std::abs(wrap_time(-3.0, 0.0) - 0.0) < kEps, "wrap_time(-3, 0) == 0");
}

static void test_shortest_circular_diff() {
    check(std::abs(shortest_circular_diff(5.0, 5.0, 10.0)) < kEps, "circ_diff(5, 5, 10) == 0");
    check(std::abs(shortest_circular_diff(7.0, 5.0, 10.0) - 2.0) < kEps, "circ_diff(7, 5, 10) == 2");
    check(std::abs(shortest_circular_diff(3.0, 5.0, 10.0) - (-2.0)) < kEps, "circ_diff(3, 5, 10) == -2");
    check(std::abs(shortest_circular_diff(1.0, 9.0, 10.0) - 2.0) < kEps, "circ_diff(1, 9, 10) == 2 (wrap fwd)");
    check(std::abs(shortest_circular_diff(9.0, 1.0, 10.0) - (-2.0)) < kEps, "circ_diff(9, 1, 10) == -2 (wrap bwd)");
    check(std::abs(shortest_circular_diff(7.0, 3.0, 0.0) - 4.0) < kEps, "circ_diff(7, 3, 0) == 4 (no wrap)");
}

// ============================================================================
// MovieTransport tests
// ============================================================================

static void test_transport_source_lifecycle() {
    MovieTransport t;
    check(t.source_generation() == 0, "initial generation == 0");

    t.set_source(10.0);
    check(t.source_generation() == 1, "set_source increments generation");
    check(std::abs(t.duration() - 10.0) < kEps, "duration set to 10.0");

    t.set_source(20.0);
    check(t.source_generation() == 2, "second set_source increments again");
    check(std::abs(t.duration() - 20.0) < kEps, "duration updated to 20.0");

    t.clear_source();
    check(t.source_generation() == 3, "clear_source increments generation");
    check(std::abs(t.duration()) < kEps, "duration reset to 0");
}

static void test_transport_frame_rate() {
    MovieTransport t;
    check(std::abs(t.frame_rate() - 30.0f) < 0.01f, "default frame_rate == 30");

    t.set_frame_rate(24.0f);
    check(std::abs(t.frame_rate() - 24.0f) < 0.01f, "frame_rate set to 24");

    // set_frame_rate does not change generation
    uint64_t gen = t.source_generation();
    t.set_frame_rate(60.0f);
    check(t.source_generation() == gen, "set_frame_rate does not change generation");
}

static void test_transport_self_clock_time() {
    MovieTransport t;
    t.set_source(10.0);

    // Normal: returns decoder_time
    check(std::abs(t.compute_self_clock_time(5.0) - 5.0) < kEps, "self_clock: normal time");
    check(std::abs(t.compute_self_clock_time(-1.0) - 0.0) < kEps, "self_clock: clamp negative");

    // HoldLast: clamp at duration
    t.set_play_mode(PlayMode::HoldLast);
    check(std::abs(t.compute_self_clock_time(15.0) - 10.0) < kEps, "self_clock: HoldLast clamps at duration");
    check(std::abs(t.compute_self_clock_time(5.0) - 5.0) < kEps, "self_clock: HoldLast passes through mid-range");

    // Loop: does not clamp
    t.set_play_mode(PlayMode::Loop);
    check(std::abs(t.compute_self_clock_time(15.0) - 15.0) < kEps, "self_clock: Loop does not clamp");
}

static void test_transport_audio_master_time() {
    MovieTransport t;
    t.set_source(10.0);

    // Basic wrapping
    check(std::abs(t.compute_audio_master_time(5.0f, 0.0) - 5.0) < kEps, "audio_master: basic");
    check(std::abs(t.compute_audio_master_time(15.0f, 0.0) - 5.0) < kEps, "audio_master: wraps at duration");

    // Phase offset
    check(std::abs(t.compute_audio_master_time(5.0f, 0.5) - 5.5) < kEps, "audio_master: phase offset");
}

static void test_transport_drift() {
    MovieTransport t;
    t.set_source(10.0);

    check(std::abs(t.drift_seconds(5.0, 5.0)) < kEps, "drift: zero when aligned");
    check(std::abs(t.drift_seconds(7.0, 5.0) - 2.0) < kEps, "drift: positive when ahead");
    check(std::abs(t.drift_seconds(3.0, 5.0) - (-2.0)) < kEps, "drift: negative when behind");
}

static void test_correction_source_change_always_seeks() {
    MovieTransport t;
    t.set_source(10.0);
    t.set_frame_rate(30.0f);

    // Zero drift but source changed → Seek
    auto d = t.evaluate_correction(5.0, 5.0, 5.0, 0.1);
    check(d.type == CorrectionType::Seek, "source_change: seeks even with zero drift");
    t.record_seek_issued(5.0);

    // Same generation, zero drift → None
    auto d2 = t.evaluate_correction(5.0, 5.0, 5.5, 0.2);
    check(d2.type == CorrectionType::None, "source_change: None when aligned");
}

static void test_correction_none_for_small_drift() {
    MovieTransport t;
    t.set_source(10.0);
    t.set_frame_rate(30.0f);

    // Consume source-change seek
    auto d0 = t.evaluate_correction(0.0, 0.0, 0.0, 0.0);
    t.record_seek_issued(0.0);

    // 30ms drift at 30fps: small threshold = 2/30 = 66.7ms. 30ms < 66.7ms → None
    auto d1 = t.evaluate_correction(5.03, 5.0, 5.2, 1.0);
    check(d1.type == CorrectionType::None, "small drift: 30ms at 30fps is None");
    check(d1.drift_seconds < 0.04, "small drift: drift_seconds reflects actual drift");
}

static void test_correction_drop_repeat_for_medium_drift() {
    MovieTransport t;
    t.set_source(10.0);
    t.set_frame_rate(30.0f);

    auto d0 = t.evaluate_correction(0.0, 0.0, 0.0, 0.0);
    t.record_seek_issued(0.0);

    // 100ms drift at 30fps: above small (66.7ms) but below seek (200ms) → DropRepeat
    auto d1 = t.evaluate_correction(5.1, 5.0, 5.5, 2.0);
    check(d1.type == CorrectionType::DropRepeat, "medium drift: 100ms is DropRepeat");
    check(!d1.budget_exhausted, "medium drift: budget not exhausted");
}

static void test_medium_drift_auto_phase_converges() {
    MovieTransport t;
    t.set_source(10.0);
    t.set_frame_rate(30.0f);

    auto d0 = t.evaluate_correction(0.0, 0.0, 0.0, 0.0);
    check(d0.type == CorrectionType::Seek, "auto_phase: initial source-change seek");
    t.record_seek_issued(0.0);

    CorrectionDecision last{};
    double desired_local = 0.0;
    for (int i = 0; i < 40; ++i) {
        desired_local = t.compute_audio_master_time(5.0f, 0.0);
        last = t.evaluate_correction(desired_local, 4.85, 5.0, 1.0 + i * (1.0 / 60.0));
    }

    check(std::abs(t.auto_phase_offset_seconds()) > 0.05,
          "auto_phase: steady medium drift learns a non-zero phase correction");
    check(last.type == CorrectionType::None || last.drift_seconds < 0.08,
          "auto_phase: steady drift converges toward the no-correction window");
}

static void test_correction_seek_for_large_drift() {
    MovieTransport t;
    t.set_source(10.0);
    t.set_frame_rate(30.0f);

    auto d0 = t.evaluate_correction(0.0, 0.0, 0.0, 0.0);
    t.record_seek_issued(0.0);

    // 300ms drift: above kSeekDriftSeconds (200ms) → Seek
    auto d1 = t.evaluate_correction(5.3, 5.0, 5.5, 2.0);
    check(d1.type == CorrectionType::Seek, "large drift: 300ms triggers Seek");
    check(std::abs(d1.seek_target - 5.3) < kEps, "large drift: seek_target is desired_local");
    check(d1.drift_seconds > 0.29, "large drift: drift_seconds reflects ~300ms");
}

static void test_correction_budget_degrades_to_drop_repeat() {
    MovieTransport t;
    t.set_source(10.0);
    t.set_frame_rate(30.0f);

    // Burn through budget: source-change + 3 more = 4 total
    auto d0 = t.evaluate_correction(0.0, 0.0, 0.0, 0.0);
    t.record_seek_issued(0.0);
    for (int i = 1; i <= 3; ++i) {
        double mono = 0.2 * i;
        auto d = t.evaluate_correction(5.3, 5.0, mono, mono);
        check(d.type == CorrectionType::Seek, "budget: seek allowed within budget");
        t.record_seek_issued(mono);
    }

    // 5th seek: budget exhausted → DropRepeat
    auto d5 = t.evaluate_correction(5.3, 5.0, 1.5, 0.95);
    check(d5.type == CorrectionType::DropRepeat, "budget: degraded to DropRepeat");
    check(d5.budget_exhausted, "budget: budget_exhausted flag set");

    // After window reset, seek works again
    auto d6 = t.evaluate_correction(5.3, 5.0, 2.0, 2.0);
    check(d6.type == CorrectionType::Seek, "budget: seek allowed after window reset");
}

static void test_correction_cooldown_degrades_to_drop_repeat() {
    MovieTransport t;
    t.set_source(10.0);
    t.set_frame_rate(30.0f);

    auto d0 = t.evaluate_correction(0.0, 0.0, 0.0, 0.0);
    t.record_seek_issued(0.0);

    // Seek with large drift at mono=1.0
    auto d1 = t.evaluate_correction(5.3, 5.0, 1.0, 1.0);
    check(d1.type == CorrectionType::Seek, "cooldown: first seek");
    t.record_seek_issued(1.0);

    // 100ms later with large drift — within 150ms cooldown → DropRepeat
    auto d2 = t.evaluate_correction(6.3, 6.0, 1.1, 1.1);
    check(d2.type == CorrectionType::DropRepeat, "cooldown: degraded to DropRepeat");

    // 300ms later — cooldown expired → Seek
    auto d3 = t.evaluate_correction(7.3, 7.0, 1.3, 1.3);
    check(d3.type == CorrectionType::Seek, "cooldown: seek after 300ms");
}

static void test_correction_frame_rate_affects_small_threshold() {
    MovieTransport t;
    t.set_source(10.0);

    // At 60fps: small threshold = 2/60 = 33.3ms
    t.set_frame_rate(60.0f);
    auto d0 = t.evaluate_correction(0.0, 0.0, 0.0, 0.0);
    t.record_seek_issued(0.0);

    // 50ms drift at 60fps: above small (33.3ms) but below seek (200ms) → DropRepeat
    auto d1 = t.evaluate_correction(5.05, 5.0, 5.5, 2.0);
    check(d1.type == CorrectionType::DropRepeat, "60fps: 50ms drift is DropRepeat");

    // Same 50ms drift at 24fps: small threshold = 2/24 = 83.3ms → None
    t.set_frame_rate(24.0f);
    auto d2 = t.evaluate_correction(5.05, 5.0, 6.0, 3.0);
    check(d2.type == CorrectionType::None, "24fps: 50ms drift is None");
}

// ============================================================================
// PlaybackSession tests
// ============================================================================

static void test_session_basics() {
    PlaybackSession s("video_node", "/path/to/video.mp4");
    check(s.operator_id() == "video_node", "session: operator id");
    check(s.source_path() == "/path/to/video.mp4", "session: source path");
    check(s.ref_count() == 0, "session: initial ref_count == 0");

    s.acquire();
    check(s.ref_count() == 1, "session: ref_count after acquire");
    s.acquire();
    check(s.ref_count() == 2, "session: ref_count after second acquire");
    s.release();
    check(s.ref_count() == 1, "session: ref_count after release");
}

// ============================================================================
// PlaybackSessionRegistry tests
// ============================================================================

static void test_registry_acquire_release() {
    auto& reg = PlaybackSessionRegistry::instance();

    auto s1 = reg.acquire("video_a", "/tmp/test_a.mp4");
    check(s1 != nullptr, "registry: acquire returns non-null");
    check(s1->ref_count() == 1, "registry: ref_count == 1 after first acquire");
    check(s1->operator_id() == "video_a", "registry: session keyed by operator id");

    // Second acquire for same operator returns same session even if the source changes.
    auto s2 = reg.acquire("video_a", "/tmp/test_b.mp4");
    check(s2.get() == s1.get(), "registry: same operator returns same session");
    check(s2->ref_count() == 2, "registry: ref_count == 2 after second acquire");
    check(s2->source_path() == "/tmp/test_b.mp4", "registry: source path updates on reacquire");

    // Different operator with the same file stays isolated.
    auto s3 = reg.acquire("video_b", "/tmp/test_b.mp4");
    check(s3.get() != s1.get(), "registry: different operator returns different session");

    // Release first session twice
    reg.release("video_a");
    check(s1->ref_count() == 1, "registry: ref_count == 1 after first release");
    reg.release("video_a");
    // After ref_count hits 0, session is removed from registry
    // A new acquire should create a fresh session
    auto s4 = reg.acquire("video_a", "/tmp/test_a.mp4");
    check(s4.get() != s1.get(), "registry: new session after full release");
    check(s4->ref_count() == 1, "registry: fresh session ref_count == 1");

    // Clean up
    reg.release("video_a");
    reg.release("video_b");
}

// ============================================================================

int main() {
    std::fprintf(stderr, "=== wrap_time / shortest_circular_diff ===\n");
    test_wrap_time();
    test_shortest_circular_diff();

    std::fprintf(stderr, "\n=== MovieTransport ===\n");
    test_transport_source_lifecycle();
    test_transport_frame_rate();
    test_transport_self_clock_time();
    test_transport_audio_master_time();
    test_transport_drift();

    std::fprintf(stderr, "\n=== Correction Tiers ===\n");
    test_correction_source_change_always_seeks();
    test_correction_none_for_small_drift();
    test_correction_drop_repeat_for_medium_drift();
    test_medium_drift_auto_phase_converges();
    test_correction_seek_for_large_drift();
    test_correction_budget_degrades_to_drop_repeat();
    test_correction_cooldown_degrades_to_drop_repeat();
    test_correction_frame_rate_affects_small_threshold();

    std::fprintf(stderr, "\n=== PlaybackSession ===\n");
    test_session_basics();

    std::fprintf(stderr, "\n=== PlaybackSessionRegistry ===\n");
    test_registry_acquire_release();

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Movie transport tests: %d passed, %d failed\n",
                 failures == 0 ? 0 : 0 /* placeholder */, failures);
    std::fprintf(stderr, "========================================\n");
    return failures == 0 ? 0 : 1;
}
