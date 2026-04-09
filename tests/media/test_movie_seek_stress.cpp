// Stress tests for the three-tier correction policy.
// Exercises adversarial drift patterns, budget exhaustion/recovery,
// cooldown enforcement, and mixed-magnitude drift sequences.

#include "movie_transport.h"
#include "test_helpers.h"

#include <cmath>
#include <cstdio>

static constexpr double kEps = 1e-9;

// Helper: set up a transport and consume the source-change seek.
static MovieTransport make_ready_transport(double duration, float fps) {
    MovieTransport t;
    t.set_source(duration);
    t.set_frame_rate(fps);
    auto d = t.evaluate_correction(0.0, 0.0, 0.0, 0.0);
    t.record_seek_issued(0.0);
    return t;
}

// ============================================================================

static void test_rapid_drift_oscillation() {
    // 100 iterations alternating +100ms/-100ms drift at 30fps.
    // 100ms is medium drift (above small=66ms, below seek=200ms).
    // After kDropRepeatEscalation (30) consecutive DropRepeats, the policy
    // escalates to Seek to resolve persistent offset.  Verify escalation
    // fires and that subsequent DropRepeats restart the counter.
    auto t = make_ready_transport(10.0, 30.0f);

    int drop_repeat_count = 0;
    int seek_count = 0;
    for (int i = 0; i < 100; ++i) {
        double sign = (i % 2 == 0) ? 1.0 : -1.0;
        double desired = 5.0 + sign * 0.1;
        double mono = 2.0 + i * 0.02; // advancing monotonically
        auto d = t.evaluate_correction(desired, 5.0, mono, mono);
        if (d.type == CorrectionType::DropRepeat) drop_repeat_count++;
        if (d.type == CorrectionType::Seek) {
            seek_count++;
            t.record_seek_issued(mono);
        }
    }
    // Escalation should fire a few times across 100 iterations
    check(seek_count > 0, "oscillation: escalation fires for persistent drift");
    check(drop_repeat_count > 50, "oscillation: most iterations are DropRepeat");
    check(drop_repeat_count + seek_count == 100, "oscillation: all accounted for");
}

static void test_budget_exhaustion_and_recovery() {
    // Run 3 cycles of: exhaust budget → verify DropRepeat → advance window → verify Seek
    auto t = make_ready_transport(10.0, 30.0f);

    for (int cycle = 0; cycle < 3; ++cycle) {
        double base_time = 10.0 * cycle;

        // Issue 4 seeks (budget max, including source-change seek consumed in make_ready
        // only matters for cycle 0 — but source-change seek was already consumed)
        // For cycle > 0, budget was reset by window advance, so 4 fresh seeks.
        int seeks_issued = 0;
        for (int i = 0; i < 4; ++i) {
            double mono = base_time + 0.2 * (i + 1);
            auto d = t.evaluate_correction(5.3, 5.0, mono, base_time + 0.2 * (i + 1));
            if (d.type == CorrectionType::Seek) {
                t.record_seek_issued(mono);
                seeks_issued++;
            }
        }
        // Should have issued some seeks (exact count depends on cooldown)

        // Now budget should be exhausted — next large drift should degrade
        double exhaust_mono = base_time + 0.95;
        auto d_exhaust = t.evaluate_correction(5.3, 5.0, exhaust_mono, base_time + 0.95);
        check(d_exhaust.type == CorrectionType::DropRepeat || d_exhaust.budget_exhausted,
              "budget_cycle: exhaustion degrades large drift");

        // Advance window past 1 second
        double next_mono = base_time + 2.0;
        auto d_recover = t.evaluate_correction(5.3, 5.0, next_mono, base_time + 2.0);
        check(d_recover.type == CorrectionType::Seek,
              "budget_cycle: seek works after window reset");
        t.record_seek_issued(next_mono);
    }
}

static void test_cooldown_prevents_rapid_seeks() {
    auto t = make_ready_transport(10.0, 30.0f);

    // Repeat 10 times: seek, attempt rapid follow-up, verify cooldown, then pass cooldown
    for (int i = 0; i < 10; ++i) {
        double base_mono = 10.0 * i + 1.0;
        double base_graph = 10.0 * i + 1.0;

        // Seek with large drift
        auto d1 = t.evaluate_correction(5.3, 5.0, base_mono, base_graph);
        if (d1.type == CorrectionType::Seek) {
            t.record_seek_issued(base_mono);
        }

        // 50ms later — within 150ms cooldown
        auto d2 = t.evaluate_correction(5.3, 5.0, base_mono + 0.05, base_graph + 0.05);
        check(d2.type != CorrectionType::Seek,
              "cooldown: no seek within 50ms");

        // 200ms later — past cooldown
        auto d3 = t.evaluate_correction(5.3, 5.0, base_mono + 0.2, base_graph + 0.2);
        check(d3.type == CorrectionType::Seek,
              "cooldown: seek after 200ms");
        t.record_seek_issued(base_mono + 0.2);
    }
}

static void test_source_change_overrides_budget() {
    auto t = make_ready_transport(10.0, 30.0f);

    // Exhaust budget
    for (int i = 0; i < 4; ++i) {
        double mono = 0.2 * (i + 1);
        auto d = t.evaluate_correction(5.3, 5.0, mono, mono);
        if (d.type == CorrectionType::Seek) t.record_seek_issued(mono);
    }

    // Verify exhausted
    auto d_ex = t.evaluate_correction(5.3, 5.0, 1.5, 0.95);
    check(d_ex.type != CorrectionType::Seek, "override: budget exhausted");

    // Source change — overrides everything
    t.set_source(20.0);
    t.set_frame_rate(30.0f);
    auto d_after = t.evaluate_correction(5.0, 5.0, 2.0, 2.0);
    check(d_after.type == CorrectionType::Seek, "override: source change overrides budget");
}

static void test_mixed_drift_magnitudes() {
    auto t = make_ready_transport(10.0, 30.0f);
    // At 30fps: small threshold = 2/30 ≈ 66.7ms, seek threshold = 200ms

    double mono = 2.0;
    auto advance = [&](double dt) { mono += dt; return mono; };

    // 50 ticks with small drift (30ms) → None
    int none_count = 0;
    for (int i = 0; i < 50; ++i) {
        auto d = t.evaluate_correction(5.03, 5.0, advance(0.02), mono);
        if (d.type == CorrectionType::None) none_count++;
    }
    check(none_count == 50, "mixed: all 50 small-drift ticks are None");

    // 10 ticks with medium drift (100ms) → DropRepeat
    int dr_count = 0;
    for (int i = 0; i < 10; ++i) {
        auto d = t.evaluate_correction(5.1, 5.0, advance(0.02), mono);
        if (d.type == CorrectionType::DropRepeat) dr_count++;
    }
    check(dr_count == 10, "mixed: all 10 medium-drift ticks are DropRepeat");

    // 1 tick with large drift (300ms) → Seek
    auto d_large = t.evaluate_correction(5.3, 5.0, advance(0.2), mono);
    check(d_large.type == CorrectionType::Seek, "mixed: large-drift tick is Seek");
    t.record_seek_issued(mono);

    // Back to small drift → None
    int none2 = 0;
    for (int i = 0; i < 10; ++i) {
        auto d = t.evaluate_correction(5.01, 5.0, advance(0.02), mono);
        if (d.type == CorrectionType::None) none2++;
    }
    check(none2 == 10, "mixed: back to None after seek");
}

// ============================================================================

int main() {
    std::fprintf(stderr, "=== Seek Stress Tests ===\n");
    test_rapid_drift_oscillation();
    test_budget_exhaustion_and_recovery();
    test_cooldown_prevents_rapid_seeks();
    test_source_change_overrides_budget();
    test_mixed_drift_magnitudes();

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Seek stress tests: %d failed\n", failures);
    std::fprintf(stderr, "========================================\n");
    return failures == 0 ? 0 : 1;
}
