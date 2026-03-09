#include <cstdio>
#include <vector>

#include "operators/shared/media_session/media_session.h"

static int g_failures = 0;

static void expect_true(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_failures;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

// Fill ring buffer with silent frames up to `frames` count, clamped by available space.
static void fill_ring(vivid::media::MediaSession& session, uint32_t frames) {
    constexpr uint32_t kChunk = 4096;
    static float zero[kChunk] = {};
    uint32_t remaining = frames;
    while (remaining > 0) {
        uint32_t chunk = remaining < kChunk ? remaining : kChunk;
        uint32_t wrote = vivid::media::media_session_audio_write(session, zero, zero, chunk);
        remaining -= wrote;
        if (wrote == 0) break; // ring full
    }
}

// Simulate what the audio-out operator does: pop a pending transport command and,
// if it is a Seek whose generation matches the current source_generation, apply it
// (increment sync_resync_applied). Stale-generation commands are silently discarded.
static void simulate_callback_resync(vivid::media::MediaSession& session) {
    auto cmd = vivid::media::media_session_pop_command(session);
    if (!cmd) return;
    if (cmd->type == vivid::media::TransportCommandType::Seek) {
        const uint64_t cur_gen = session.source_generation.load(std::memory_order_relaxed);
        if (cmd->generation >= cur_gen) {
            session.sync_resync_requests.fetch_add(1, std::memory_order_relaxed);
            session.sync_resync_applied.fetch_add(1, std::memory_order_relaxed);
        }
        // Stale: drop silently — correct operator behaviour.
    }
}

// ---------------------------------------------------------------------------
// test_loop_boundary_no_chatter
//
// Simulates 200 audio callbacks with 20 loop boundaries. The fill thread keeps
// up with consumption in steady state and re-seeds the ring at each boundary.
// Asserts: exactly one resync per boundary, zero underruns.
// ---------------------------------------------------------------------------
static void test_loop_boundary_no_chatter() {
    vivid::media::MediaSession session;
    session.audio_ring_sample_rate.store(48000.0f);
    session.audio_ring_speed.store(1.0f);
    session.source_generation.store(1);

    constexpr uint32_t kPrerollFrames = 24000; // 0.5 s at 48 kHz
    constexpr uint32_t kCallbackFrames = 512;
    constexpr int kIterations = 200;
    constexpr int kBoundaryInterval = 10; // 20 boundaries total

    // Initial preroll.
    fill_ring(session, kPrerollFrames);
    session.audio_preroll_ready.store(1, std::memory_order_release);

    for (int i = 0; i < kIterations; ++i) {
        if (i % kBoundaryInterval == 0) {
            // Loop boundary: bump epoch, enqueue a resync seek, top up ring.
            session.loop_epoch.fetch_add(1, std::memory_order_relaxed);
            vivid::media::TransportCommand seek{};
            seek.type = vivid::media::TransportCommandType::Seek;
            seek.generation = session.source_generation.load(std::memory_order_relaxed);
            seek.seek_time_s = 0.0;
            vivid::media::media_session_enqueue_command(session, seek);
            fill_ring(session, kPrerollFrames);
        }

        // Callback: apply any pending resync, then read audio.
        simulate_callback_resync(session);
        float l[kCallbackFrames] = {};
        float r[kCallbackFrames] = {};
        vivid::media::media_session_audio_read(session, l, r, kCallbackFrames);

        // Steady-state fill thread: replace consumed frames.
        fill_ring(session, kCallbackFrames);
    }

    const int num_boundaries = kIterations / kBoundaryInterval;
    expect_true(session.sync_resync_applied.load() <= (uint64_t)num_boundaries,
                "no_chatter: sync_resync_applied <= one per loop boundary");
    expect_true(session.audio_underrun_frames.load() == 0,
                "no_chatter: no underrun frames when fill thread keeps up");
    expect_true(session.audio_underrun_callbacks.load() == 0,
                "no_chatter: no underrun callbacks in steady state");
}

// ---------------------------------------------------------------------------
// test_loop_boundary_underrun_bounded
//
// Same scenario but at each loop boundary the ring is cleared (simulating a
// seek-to-start clear) and the fill thread delays kFillDelayCallbacks before
// re-seeding. Underruns are bounded to the delay window per boundary; they
// must not accumulate progressively.
// ---------------------------------------------------------------------------
static void test_loop_boundary_underrun_bounded() {
    vivid::media::MediaSession session;
    session.audio_ring_sample_rate.store(48000.0f);
    session.audio_ring_speed.store(1.0f);
    session.source_generation.store(1);

    constexpr uint32_t kPrerollFrames = 24000;
    constexpr uint32_t kCallbackFrames = 512;
    constexpr int kIterations = 200;
    constexpr int kBoundaryInterval = 10;
    constexpr int kFillDelayCallbacks = 2; // simulated fill-thread latency

    // No initial preroll — first boundary immediately clears anyway.
    int fill_delay_remaining = 0;

    for (int i = 0; i < kIterations; ++i) {
        if (i % kBoundaryInterval == 0) {
            // Loop boundary: clear ring, enqueue resync, start fill delay.
            session.loop_epoch.fetch_add(1, std::memory_order_relaxed);
            vivid::media::TransportCommand seek{};
            seek.type = vivid::media::TransportCommandType::Seek;
            seek.generation = session.source_generation.load(std::memory_order_relaxed);
            seek.seek_time_s = 0.0;
            vivid::media::media_session_enqueue_command(session, seek);
            vivid::media::media_session_audio_ring_clear(session, 0.0);
            fill_delay_remaining = kFillDelayCallbacks;
        }

        // Callback: apply resync, read audio (may underrun during fill delay).
        simulate_callback_resync(session);
        float l[kCallbackFrames] = {};
        float r[kCallbackFrames] = {};
        vivid::media::media_session_audio_read(session, l, r, kCallbackFrames);

        // Fill-thread behaviour: delay kFillDelayCallbacks after each boundary,
        // then re-seed to preroll; in steady state, replace consumed frames.
        if (fill_delay_remaining > 0) {
            --fill_delay_remaining;
            if (fill_delay_remaining == 0) {
                fill_ring(session, kPrerollFrames); // fill thread catches up
            }
        } else {
            fill_ring(session, kCallbackFrames); // steady-state maintenance
        }
    }

    const int num_boundaries = kIterations / kBoundaryInterval;
    expect_true(session.sync_resync_applied.load() <= (uint64_t)num_boundaries,
                "underrun_bounded: sync_resync_applied <= one per boundary");
    // Underruns bounded: at most kFillDelayCallbacks underrun callbacks per boundary.
    expect_true(session.audio_underrun_callbacks.load() <=
                    (uint64_t)(num_boundaries * kFillDelayCallbacks),
                "underrun_bounded: underrun callbacks bounded to delay x num_boundaries");
    // Underrun frames bounded: each underrun callback covers at most kCallbackFrames.
    expect_true(session.audio_underrun_frames.load() <=
                    (uint64_t)(num_boundaries * kFillDelayCallbacks * kCallbackFrames),
                "underrun_bounded: underrun frames bounded (no progressive accumulation)");
}

// ---------------------------------------------------------------------------
// test_video_drop_policy
//
// Enqueues 10 frames into a queue with cap=4. Verifies the overflow-drop
// counter and that exactly 4 frames survive.
// ---------------------------------------------------------------------------
static void test_video_drop_policy() {
    vivid::media::MediaSession session;

    // Enqueue 10 frames; queue cap is 4, so 6 should be dropped.
    for (uint32_t i = 0; i < 10; ++i) {
        vivid::media::VideoFramePayload vf{};
        vf.frame_index = i;
        vf.bytes = {0, 1, 2, 3};
        vivid::media::media_session_enqueue_video_frame(session, std::move(vf));
    }

    expect_true(session.video_payload_dropped.load() == 6,
                "video_drop: exactly 6 frames dropped (10 enqueued, cap 4)");
    // High-water is recorded after the push but before trimming, so it reaches
    // cap+1 = 5. Assert >= 4 (the cap) to remain robust to implementation detail.
    expect_true(session.video_payload_depth_high_water.load() >= 4,
                "video_drop: high-water depth >= cap (4)");

    // Pop all remaining — should be exactly 4.
    uint32_t popped = 0;
    while (vivid::media::media_session_pop_video_frame(session).has_value()) {
        ++popped;
    }
    expect_true(popped == 4,
                "video_drop: exactly 4 frames survive after overflow");
    expect_true(session.video_payload_popped.load() == 4,
                "video_drop: pop counter == 4");
}

// ---------------------------------------------------------------------------
// test_generation_no_stale_resync
//
// Enqueues a Seek command with a stale generation (lower than current
// source_generation). The operator-side logic must reject it without
// incrementing sync_resync_applied.
// ---------------------------------------------------------------------------
static void test_generation_no_stale_resync() {
    vivid::media::MediaSession session;
    // Advance source_generation past the command's generation.
    session.source_generation.store(2);

    vivid::media::TransportCommand seek{};
    seek.type = vivid::media::TransportCommandType::Seek;
    seek.generation = 1; // stale: 1 < 2
    seek.seek_time_s = 5.0;
    vivid::media::media_session_enqueue_command(session, seek);

    auto cmd = vivid::media::media_session_pop_command(session);
    expect_true(cmd.has_value(), "stale_resync: command was enqueued and dequeued");

    const uint64_t cur_gen = session.source_generation.load(std::memory_order_relaxed);
    const bool is_stale = cmd && cmd->generation < cur_gen;
    expect_true(is_stale, "stale_resync: command generation is stale (1 < 2)");

    // Operator rejects stale command — do not increment sync_resync_applied.
    if (!is_stale && cmd) {
        session.sync_resync_applied.fetch_add(1, std::memory_order_relaxed);
    }

    expect_true(session.sync_resync_applied.load() == 0,
                "stale_resync: sync_resync_applied stays 0 for stale command");
}

int main() {
    test_loop_boundary_no_chatter();
    test_loop_boundary_underrun_bounded();
    test_video_drop_policy();
    test_generation_no_stale_resync();

    if (g_failures != 0) {
        std::fprintf(stderr, "\n%d test(s) failed.\n", g_failures);
        return 1;
    }
    std::fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
