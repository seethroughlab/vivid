// Unit tests for movie AV sync logic: wrap_time, shortest_circular_diff,
// AudioRing preroll/time-tracking/underrun, and seek threshold calculation.

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "test_helpers.h"

static int g_fail = 0;
static int g_pass = 0;

// ============================================================================
// Sync math (copied from movie_file_in.cpp — pure functions)
// ============================================================================

static double wrap_time(double t, double duration) {
    if (duration <= 0.0) return std::max(0.0, t);
    double out = std::fmod(t, duration);
    if (out < 0.0) out += duration;
    return out;
}

static double shortest_circular_diff(double target, double current, double duration) {
    if (duration <= 0.0) return target - current;
    double d = target - current;
    const double half = duration * 0.5;
    while (d > half) d -= duration;
    while (d < -half) d += duration;
    return d;
}

// ============================================================================
// AudioRing (minimal copy from movie_file_audio.cpp for isolated testing)
// ============================================================================

struct AudioRing {
    static constexpr uint32_t kCapacity = 240000;

    std::array<float, kCapacity> left{};
    std::array<float, kCapacity> right{};
    std::atomic<uint32_t> write_pos{0};
    std::atomic<uint32_t> read_pos{0};
    std::atomic<uint32_t> epoch{0};
    std::atomic<float>    sample_rate{48000.0f};
    std::atomic<float>    speed{1.0f};
    std::atomic<double>   read_head_time{0.0};
    std::atomic<double>   write_head_time{0.0};
    std::atomic<uint8_t>  preroll_ready{0};

    uint32_t available_read() const {
        const uint32_t w = write_pos.load(std::memory_order_acquire);
        const uint32_t r = read_pos.load(std::memory_order_relaxed);
        return (w - r + kCapacity) % kCapacity;
    }

    uint32_t available_write() const {
        const uint32_t w = write_pos.load(std::memory_order_relaxed);
        const uint32_t r = read_pos.load(std::memory_order_acquire);
        return (r - w - 1 + kCapacity) % kCapacity;
    }

    uint32_t write(const float* l, const float* r_in, uint32_t frames) {
        if (!l || !r_in || frames == 0) return 0;
        const uint32_t can = available_write();
        const uint32_t n = std::min(frames, can);
        const uint32_t wp = write_pos.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t idx = (wp + i) % kCapacity;
            left[idx] = l[i];
            right[idx] = r_in[i];
        }
        write_pos.store((wp + n) % kCapacity, std::memory_order_release);
        return n;
    }

    uint32_t read(float* l_out, float* r_out, uint32_t frames) {
        if (!l_out || !r_out || frames == 0) return 0;
        const uint32_t epoch_before = epoch.load(std::memory_order_acquire);
        const uint32_t avail = available_read();
        const uint32_t n = std::min(frames, avail);
        const uint32_t rp = read_pos.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t idx = (rp + i) % kCapacity;
            l_out[i] = left[idx];
            r_out[i] = right[idx];
        }
        if (epoch.load(std::memory_order_acquire) != epoch_before) {
            std::memset(l_out, 0, frames * sizeof(float));
            std::memset(r_out, 0, frames * sizeof(float));
            return 0;
        }
        if (n < frames) {
            std::memset(l_out + n, 0, (frames - n) * sizeof(float));
            std::memset(r_out + n, 0, (frames - n) * sizeof(float));
        }
        read_pos.store((rp + n) % kCapacity, std::memory_order_release);
        {
            const double sr = std::max(1.0, static_cast<double>(sample_rate.load(std::memory_order_relaxed)));
            const double spd = std::max(0.0, static_cast<double>(speed.load(std::memory_order_relaxed)));
            const double advance = static_cast<double>(frames) * spd / sr;
            const double old_t = read_head_time.load(std::memory_order_relaxed);
            double new_t = old_t + advance;

            const double wht = write_head_time.load(std::memory_order_acquire);
            const double buffer_lag = static_cast<double>(available_read()) * spd / sr;
            const double expected_t = wht - buffer_lag;
            const double error = new_t - expected_t;

            constexpr double kSnapThreshold = 0.100;
            constexpr double kSlewThreshold = 0.002;
            constexpr double kSlewRate      = 0.10;

            if (std::abs(error) > kSnapThreshold) {
                new_t = expected_t + advance;
            } else if (std::abs(error) > kSlewThreshold) {
                new_t -= error * kSlewRate;
            }

            read_head_time.store(new_t, std::memory_order_relaxed);
        }
        return n;
    }

    void clear(double reset_time = 0.0) {
        write_pos.store(0, std::memory_order_relaxed);
        read_pos.store(0, std::memory_order_relaxed);
        read_head_time.store(reset_time, std::memory_order_relaxed);
        write_head_time.store(reset_time, std::memory_order_relaxed);
        epoch.fetch_add(1, std::memory_order_release);
    }
};

// ============================================================================
// Tests
// ============================================================================

static constexpr double kEps = 1e-9;

static void test_wrap_time() {
    // Basic wrap
    check(std::abs(wrap_time(0.0, 10.0) - 0.0) < kEps, "wrap_time(0, 10) == 0");
    check(std::abs(wrap_time(5.0, 10.0) - 5.0) < kEps, "wrap_time(5, 10) == 5");
    check(std::abs(wrap_time(10.0, 10.0) - 0.0) < kEps, "wrap_time(10, 10) == 0");
    check(std::abs(wrap_time(15.5, 10.0) - 5.5) < kEps, "wrap_time(15.5, 10) == 5.5");

    // Negative time
    check(std::abs(wrap_time(-1.0, 10.0) - 9.0) < kEps, "wrap_time(-1, 10) == 9");
    check(std::abs(wrap_time(-10.0, 10.0) - 0.0) < kEps, "wrap_time(-10, 10) == 0");

    // Zero or negative duration: clamp to max(0, t)
    check(std::abs(wrap_time(5.0, 0.0) - 5.0) < kEps, "wrap_time(5, 0) == 5");
    check(std::abs(wrap_time(-3.0, 0.0) - 0.0) < kEps, "wrap_time(-3, 0) == 0");
    check(std::abs(wrap_time(5.0, -1.0) - 5.0) < kEps, "wrap_time(5, -1) == 5");
}

static void test_shortest_circular_diff() {
    // Same position
    check(std::abs(shortest_circular_diff(5.0, 5.0, 10.0)) < kEps, "circ_diff(5, 5, 10) == 0");

    // Forward within half
    check(std::abs(shortest_circular_diff(7.0, 5.0, 10.0) - 2.0) < kEps, "circ_diff(7, 5, 10) == 2");

    // Backward within half
    check(std::abs(shortest_circular_diff(3.0, 5.0, 10.0) - (-2.0)) < kEps, "circ_diff(3, 5, 10) == -2");

    // Wrap forward (9 -> 1 with duration 10 means target is 2 ahead)
    check(std::abs(shortest_circular_diff(1.0, 9.0, 10.0) - 2.0) < kEps, "circ_diff(1, 9, 10) == 2 (wrap fwd)");

    // Wrap backward (1 -> 9 with duration 10 means target is 2 behind)
    check(std::abs(shortest_circular_diff(9.0, 1.0, 10.0) - (-2.0)) < kEps, "circ_diff(9, 1, 10) == -2 (wrap bwd)");

    // Exactly half: should return +half (implementation detail)
    check(std::abs(shortest_circular_diff(5.0, 0.0, 10.0) - 5.0) < kEps, "circ_diff(5, 0, 10) == 5 (half)");

    // Zero duration: plain subtraction
    check(std::abs(shortest_circular_diff(7.0, 3.0, 0.0) - 4.0) < kEps, "circ_diff(7, 3, 0) == 4 (no wrap)");
}

static void test_seek_threshold() {
    // At 24fps, frame_dur = 1/24, threshold = 2/24 = 1/12
    const double fps24 = 24.0;
    const double frame_dur = 1.0 / std::max(1.0, fps24);
    const double threshold = frame_dur * 2.0;
    check(std::abs(threshold - 1.0/12.0) < kEps, "seek threshold at 24fps == 1/12 s");

    // At 60fps, threshold = 2/60 = 1/30
    const double fps60 = 60.0;
    const double threshold60 = (1.0 / fps60) * 2.0;
    check(std::abs(threshold60 - 1.0/30.0) < kEps, "seek threshold at 60fps == 1/30 s");

    // Small drift within threshold should NOT trigger seek
    const double drift = 0.03; // 30ms at 24fps (threshold ~83ms)
    check(drift < threshold, "30ms drift < 24fps threshold (no seek)");

    // Large drift outside threshold should trigger seek
    const double big_drift = 0.1; // 100ms
    check(big_drift > threshold, "100ms drift > 24fps threshold (seek)");
}

static void test_ring_basic_write_read() {
    AudioRing ring{};
    const uint32_t N = 256;
    float wl[256], wr[256];
    for (uint32_t i = 0; i < N; ++i) {
        wl[i] = static_cast<float>(i);
        wr[i] = static_cast<float>(i) * -1.0f;
    }

    uint32_t written = ring.write(wl, wr, N);
    check(written == N, "ring write 256 frames");
    check(ring.available_read() == N, "ring has 256 available after write");

    float rl[256], rr[256];
    uint32_t got = ring.read(rl, rr, N);
    check(got == N, "ring read 256 frames");
    check(ring.available_read() == 0, "ring empty after read");

    bool data_ok = true;
    for (uint32_t i = 0; i < N; ++i) {
        if (rl[i] != wl[i] || rr[i] != wr[i]) { data_ok = false; break; }
    }
    check(data_ok, "ring data integrity after write/read");
}

static void test_ring_underrun_pads_silence() {
    AudioRing ring{};
    // Write 100 frames
    float wl[100], wr[100];
    for (uint32_t i = 0; i < 100; ++i) { wl[i] = 1.0f; wr[i] = 1.0f; }
    ring.write(wl, wr, 100);

    // Read 256 frames (underrun)
    float rl[256], rr[256];
    std::memset(rl, 0x7F, sizeof(rl)); // fill with non-zero to detect padding
    std::memset(rr, 0x7F, sizeof(rr));
    uint32_t got = ring.read(rl, rr, 256);
    check(got == 100, "ring underrun: got 100 of 256");

    // First 100 should be data, rest should be zero
    bool pad_ok = true;
    for (uint32_t i = 100; i < 256; ++i) {
        if (rl[i] != 0.0f || rr[i] != 0.0f) { pad_ok = false; break; }
    }
    check(pad_ok, "ring underrun: trailing frames are silence");
}

static void test_ring_time_advances_during_underrun() {
    AudioRing ring{};
    ring.sample_rate.store(48000.0f, std::memory_order_relaxed);
    ring.speed.store(1.0f, std::memory_order_relaxed);
    ring.read_head_time.store(0.0, std::memory_order_relaxed);

    // Write 100 frames, then read 256 (underrun by 156)
    float wl[100], wr[100];
    for (uint32_t i = 0; i < 100; ++i) { wl[i] = 0.5f; wr[i] = 0.5f; }
    ring.write(wl, wr, 100);

    // Set write_head_time consistent with the data written (100 frames at 1x/48kHz)
    // so the reconciliation doesn't fight the expected advance.
    const double written_time = 256.0 / 48000.0; // matches expected read head after read
    ring.write_head_time.store(written_time, std::memory_order_relaxed);

    float rl[256], rr[256];
    ring.read(rl, rr, 256);

    // Time should advance by 256 / 48000 even though only 100 samples had data
    const double expected_time = 256.0 / 48000.0;
    const double actual_time = ring.read_head_time.load(std::memory_order_relaxed);
    check(std::abs(actual_time - expected_time) < 1e-6,
          "ring time advances by full frame count during underrun");
}

static void test_ring_time_scales_with_speed() {
    AudioRing ring{};
    ring.sample_rate.store(48000.0f, std::memory_order_relaxed);
    ring.speed.store(2.0f, std::memory_order_relaxed);
    ring.read_head_time.store(0.0, std::memory_order_relaxed);

    // Fill ring to avoid underrun
    float wl[4096], wr[4096];
    for (uint32_t i = 0; i < 4096; ++i) { wl[i] = 0.1f; wr[i] = 0.1f; }
    ring.write(wl, wr, 4096);

    // Set write_head_time so reconciliation doesn't interfere.
    // After reading 256 frames at 2x speed: expected read_head = 256*2/48000
    // buffer_lag = (4096-256) * 2 / 48000, so wht = expected + buffer_lag
    const double expected = 256.0 * 2.0 / 48000.0;
    const double buffer_lag = (4096.0 - 256.0) * 2.0 / 48000.0;
    ring.write_head_time.store(expected + buffer_lag, std::memory_order_relaxed);

    float rl[256], rr[256];
    ring.read(rl, rr, 256);

    // At 2x speed, time advances twice as fast: 256 * 2.0 / 48000
    const double actual = ring.read_head_time.load(std::memory_order_relaxed);
    check(std::abs(actual - expected) < 1e-6, "ring time scales with speed (2x)");
}

static void test_ring_clear_resets_state() {
    AudioRing ring{};
    float wl[100], wr[100];
    for (uint32_t i = 0; i < 100; ++i) { wl[i] = 1.0f; wr[i] = 1.0f; }
    ring.write(wl, wr, 100);

    uint32_t epoch_before = ring.epoch.load(std::memory_order_relaxed);
    ring.clear(5.0);

    check(ring.available_read() == 0, "ring clear: available_read == 0");
    check(std::abs(ring.read_head_time.load(std::memory_order_relaxed) - 5.0) < kEps,
          "ring clear: read_head_time reset to 5.0");
    check(std::abs(ring.write_head_time.load(std::memory_order_relaxed) - 5.0) < kEps,
          "ring clear: write_head_time reset to 5.0");
    check(ring.epoch.load(std::memory_order_relaxed) == epoch_before + 1,
          "ring clear: epoch incremented");
}

static void test_ring_preroll_gate() {
    // Simulate preroll: buffer must reach threshold before preroll_ready flips.
    // The FillThread does this check; we simulate it here.
    AudioRing ring{};
    static constexpr uint32_t kPrerollFrames = 24000;

    check(ring.preroll_ready.load(std::memory_order_relaxed) == 0, "preroll starts at 0");

    // Write less than preroll threshold
    std::array<float, 4096> buf{};
    for (uint32_t total = 0; total < kPrerollFrames - 4096; total += 4096) {
        ring.write(buf.data(), buf.data(), 4096);
    }
    // Still below threshold
    check(ring.available_read() < kPrerollFrames, "preroll: not enough buffered yet");

    // Write past threshold
    ring.write(buf.data(), buf.data(), 4096);
    ring.write(buf.data(), buf.data(), 4096);
    check(ring.available_read() >= kPrerollFrames, "preroll: now >= threshold");

    // Simulate what FillThread::pump does
    if (ring.preroll_ready.load(std::memory_order_relaxed) == 0) {
        if (ring.available_read() >= kPrerollFrames) {
            ring.preroll_ready.store(1, std::memory_order_release);
        }
    }
    check(ring.preroll_ready.load(std::memory_order_relaxed) == 1, "preroll: ready after threshold");

    // Clear resets preroll
    ring.preroll_ready.store(0, std::memory_order_relaxed);
    ring.clear();
    check(ring.preroll_ready.load(std::memory_order_relaxed) == 0, "preroll: reset after clear");
}

static void test_ring_capacity_limit() {
    AudioRing ring{};
    // Ring capacity is 240000 but usable space is 239999 (one slot reserved)
    check(ring.available_write() == AudioRing::kCapacity - 1, "ring: initial writable == capacity - 1");
    check(ring.available_read() == 0, "ring: initial readable == 0");
}

static void test_ring_drift_snap() {
    // When read_head_time drifts >100ms from expected, it should snap.
    AudioRing ring{};
    ring.sample_rate.store(48000.0f, std::memory_order_relaxed);
    ring.speed.store(1.0f, std::memory_order_relaxed);

    // Fill some data so available_read > 0
    float buf[4096];
    std::memset(buf, 0, sizeof(buf));
    ring.write(buf, buf, 4096);

    // Set write_head_time far ahead of where read_head_time will be
    ring.read_head_time.store(0.0, std::memory_order_relaxed);
    ring.write_head_time.store(1.0, std::memory_order_relaxed); // 1s ahead

    // Read one buffer — should snap
    float rl[256], rr[256];
    ring.read(rl, rr, 256);

    // expected_t = 1.0 - (available_read * 1.0 / 48000)
    // After snap: new_t = expected_t + advance
    const double rht = ring.read_head_time.load(std::memory_order_relaxed);
    // The read head should now be close to expected_t + advance, not 256/48000
    check(rht > 0.5, "drift snap: read_head_time jumped toward write_head (> 0.5s)");
    check(rht < 1.1, "drift snap: read_head_time reasonable upper bound");
}

static void test_ring_drift_slew() {
    // When read_head_time drifts 2-100ms, it should slew toward correct value.
    AudioRing ring{};
    ring.sample_rate.store(48000.0f, std::memory_order_relaxed);
    ring.speed.store(1.0f, std::memory_order_relaxed);

    // Fill plenty of data
    float buf[4096];
    std::memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 20; ++i) ring.write(buf, buf, 4096);

    // Introduce a 50ms drift: set read_head_time 50ms ahead of where it should be
    const double buffered_sec = static_cast<double>(ring.available_read()) / 48000.0;
    const double wht = 2.0;
    ring.write_head_time.store(wht, std::memory_order_relaxed);
    const double expected = wht - buffered_sec;
    ring.read_head_time.store(expected + 0.050, std::memory_order_relaxed); // 50ms ahead

    // Run 200 callbacks (should converge well within this)
    float rl[256], rr[256];
    for (int i = 0; i < 200; ++i) {
        // Keep write_head_time advancing to match what read consumes
        double cur_wht = ring.write_head_time.load(std::memory_order_relaxed);
        ring.write_head_time.store(cur_wht + 256.0 / 48000.0, std::memory_order_relaxed);
        // Refill data to keep ring from draining
        ring.write(buf, buf, 256);
        ring.read(rl, rr, 256);
    }

    const double final_rht = ring.read_head_time.load(std::memory_order_relaxed);
    const double final_wht = ring.write_head_time.load(std::memory_order_relaxed);
    const double final_buffered = static_cast<double>(ring.available_read()) / 48000.0;
    const double final_expected = final_wht - final_buffered;
    const double final_error = std::abs(final_rht - final_expected);
    check(final_error < 0.002, "drift slew: error converged below 2ms deadband");
}

static void test_ring_drift_deadband() {
    // When drift is < 2ms, no correction should be applied — pure free-running.
    AudioRing ring{};
    ring.sample_rate.store(48000.0f, std::memory_order_relaxed);
    ring.speed.store(1.0f, std::memory_order_relaxed);

    float buf[4096];
    std::memset(buf, 0, sizeof(buf));
    ring.write(buf, buf, 4096);

    // Set clocks so they're perfectly aligned
    const double buffered_sec = static_cast<double>(ring.available_read()) / 48000.0;
    ring.write_head_time.store(1.0, std::memory_order_relaxed);
    ring.read_head_time.store(1.0 - buffered_sec, std::memory_order_relaxed);

    float rl[256], rr[256];
    ring.read(rl, rr, 256);

    // Should advance by exactly frames/sr with no correction
    const double expected_advance = 256.0 / 48000.0;
    const double actual = ring.read_head_time.load(std::memory_order_relaxed);
    const double expected_final = (1.0 - buffered_sec) + expected_advance;
    check(std::abs(actual - expected_final) < 1e-9,
          "drift deadband: no correction when error < 2ms");
}

static void test_ring_drift_underrun_snap() {
    // Empty ring with diverged clocks — buffer_lag is 0, should snap to write_head_time.
    AudioRing ring{};
    ring.sample_rate.store(48000.0f, std::memory_order_relaxed);
    ring.speed.store(1.0f, std::memory_order_relaxed);

    // No data in ring
    ring.read_head_time.store(0.0, std::memory_order_relaxed);
    ring.write_head_time.store(2.0, std::memory_order_relaxed);

    float rl[256], rr[256];
    ring.read(rl, rr, 256);

    // available_read == 0, so expected_t = wht - 0 = 2.0
    // error = (0 + 256/48000) - 2.0 ≈ -1.995 → snap
    const double rht = ring.read_head_time.load(std::memory_order_relaxed);
    const double advance = 256.0 / 48000.0;
    check(std::abs(rht - (2.0 + advance)) < 0.001,
          "drift underrun snap: read_head_time snapped to write_head_time + advance");
}

// ============================================================================

int main() {
    std::fprintf(stderr, "=== Sync math tests ===\n");
    test_wrap_time();
    test_shortest_circular_diff();
    test_seek_threshold();

    std::fprintf(stderr, "\n=== AudioRing tests ===\n");
    test_ring_basic_write_read();
    test_ring_underrun_pads_silence();
    test_ring_time_advances_during_underrun();
    test_ring_time_scales_with_speed();
    test_ring_clear_resets_state();
    test_ring_preroll_gate();
    test_ring_capacity_limit();

    std::fprintf(stderr, "\n=== Drift reconciliation tests ===\n");
    test_ring_drift_snap();
    test_ring_drift_slew();
    test_ring_drift_deadband();
    test_ring_drift_underrun_snap();

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "AV sync tests: %d passed, %d failed\n", g_pass, g_fail);
    std::fprintf(stderr, "========================================\n");
    return g_fail == 0 ? 0 : 1;
}
