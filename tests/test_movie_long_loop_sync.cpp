#include "operators/shared/movie_audio/sync_policy.h"

#include <cstdio>
#include <cmath>

static int g_failures = 0;

static void expect_true(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_failures;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

int main() {
    constexpr AVSyncThresholds t{};
    AVSyncCorrectionMode mode = AVSyncCorrectionMode::Locked;

    constexpr double kSampleRate = 48000.0;
    constexpr double kBlock = 256.0;
    constexpr double kDt = kBlock / kSampleRate;
    constexpr uint64_t kCallbacks = 12000; // ~64 seconds
    constexpr uint64_t kStartupGateCallbacks = 24;
    constexpr uint64_t kLoopGateCallbacks = 16;
    constexpr uint64_t kLoopPeriodCallbacks = 300; // ~1.6 seconds

    uint64_t startup_gate_until = kStartupGateCallbacks;
    uint64_t loop_gate_until = 0;
    uint64_t loop_epoch = 0;

    uint64_t none_count = 0;
    uint64_t skip_count = 0;
    uint64_t silence_count = 0;
    uint64_t resync_count = 0;
    uint64_t corrective_in_gate_count = 0;
    uint64_t loop_transition_count = 0;

    for (uint64_t cb = 0; cb < kCallbacks; ++cb) {
        if (cb > 0 && (cb % kLoopPeriodCallbacks) == 0) {
            loop_epoch++;
            loop_gate_until = cb + kLoopGateCallbacks;
            loop_transition_count++;
        }

        const bool gate_on = sync_gate_active(cb, startup_gate_until) ||
                             sync_gate_active(cb, loop_gate_until);

        // Base jitter around lock.
        double error_s = 0.004 * std::sin(static_cast<double>(cb) * 0.07);

        // Periodic behind bursts should produce skip, except during active gate.
        if ((cb % 500) < 20) {
            error_s += 0.050;
        }
        // Near-threshold ahead jitter should stay non-corrective while locked.
        if ((cb % 700) < 20) {
            error_s -= 0.031;
        }
        // Immediately after loop transitions, inject larger drift and verify gate suppression.
        if (loop_epoch > 0 && (cb % kLoopPeriodCallbacks) < 8) {
            error_s += 0.090;
        }

        const auto d = decide_av_sync_stateful_gated(error_s, t, mode, gate_on);
        if (gate_on && d.action != AVSyncAction::None) {
            corrective_in_gate_count++;
        }

        switch (d.action) {
            case AVSyncAction::None:    none_count++; break;
            case AVSyncAction::Skip:    skip_count++; break;
            case AVSyncAction::Silence: silence_count++; break;
            case AVSyncAction::Resync:  resync_count++; break;
        }
    }

    std::fprintf(stderr,
                 "summary: none=%llu skip=%llu silence=%llu resync=%llu gate_corrective=%llu loops=%llu\n",
                 static_cast<unsigned long long>(none_count),
                 static_cast<unsigned long long>(skip_count),
                 static_cast<unsigned long long>(silence_count),
                 static_cast<unsigned long long>(resync_count),
                 static_cast<unsigned long long>(corrective_in_gate_count),
                 static_cast<unsigned long long>(loop_transition_count));

    expect_true(loop_transition_count > 0, "loop transitions exercised");
    expect_true(skip_count > 0, "skip correction exercised");
    expect_true(silence_count == 0, "near-threshold ahead drift does not trigger hold/silence chatter");
    expect_true(resync_count == 0, "no hard resync under long-loop moderate drift");
    expect_true(corrective_in_gate_count == 0, "startup/loop gates suppress corrective actions");

    return g_failures == 0 ? 0 : 1;
}
