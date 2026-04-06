#include "operator_api/child_op.h"
#include "operators/child_test_op.h"
#include "control/lfo/lfo.h"
#include "control/smooth/smooth.h"
#include <cstdio>
#include <cmath>
#include "test_helpers.h"

// Helper to build a minimal parent context
static VividFrameContext make_ctx(double time, double dt, uint64_t frame) {
    VividFrameContext ctx{};
    ctx.time       = time;
    ctx.delta_time = dt;
    ctx.frame      = frame;
    return ctx;
}

static void set_metronome(VividFrameContext& ctx, bool enabled, float bpm,
                          uint32_t beats_per_bar, double beats_elapsed,
                          float beat_phase, float bar_phase) {
    ctx.metronome_enabled = enabled ? 1u : 0u;
    ctx.metronome_bpm = bpm;
    ctx.metronome_beats_per_bar = beats_per_bar;
    ctx.metronome_beats_elapsed = beats_elapsed;
    ctx.metronome_beat_phase = beat_phase;
    ctx.metronome_bar_phase = bar_phase;
    ctx.metronome_beat_ms = bpm > 0.0f ? 60000.0f / bpm : 0.0f;
}

// =====================================================================
// Test 1: Basic param/input/output with ChildTestOp
// =====================================================================
static void test_basic() {
    std::fprintf(stderr, "\n=== ChildOp Basic Test ===\n");

    vivid::ChildOp<ChildTestOp> child;

    check(child.param_count() == 1, "ChildTestOp has 1 param");
    check(child.input_count() == 1, "ChildTestOp has 1 input");
    check(child.output_count() == 1, "ChildTestOp has 1 output");

    // Set gain=3.0, input=5.0, expect output=15.0
    child.set_param("gain", 3.0f);
    child.set_input("input", 5.0f);

    auto ctx = make_ctx(0.0, 1.0 / 60.0, 0);
    child.process(&ctx);

    check_float(child.output("value"), 15.0f, 1e-6f, "output = input * gain = 15.0");
}

// =====================================================================
// Test 2: Index-based access
// =====================================================================
static void test_index_access() {
    std::fprintf(stderr, "\n=== ChildOp Index Access Test ===\n");

    vivid::ChildOp<ChildTestOp> child;

    child.set_param(uint32_t(0), 2.0f);
    child.set_input(uint32_t(0), 7.0f);

    auto ctx = make_ctx(0.0, 1.0 / 60.0, 0);
    child.process(&ctx);

    check_float(child.output(uint32_t(0)), 14.0f, 1e-6f, "index-based: output = 7 * 2 = 14");
}

// =====================================================================
// Test 3: Chaining — output of child A feeds input of child B
// =====================================================================
static void test_chaining() {
    std::fprintf(stderr, "\n=== ChildOp Chaining Test ===\n");

    vivid::ChildOp<ChildTestOp> a;
    vivid::ChildOp<ChildTestOp> b;

    // A: gain=2, input=3 -> output=6
    // B: gain=4, input=6 -> output=24
    a.set_param("gain", 2.0f);
    a.set_input("input", 3.0f);

    auto ctx = make_ctx(0.0, 1.0 / 60.0, 0);
    a.process(&ctx);

    float a_out = a.output("value");
    check_float(a_out, 6.0f, 1e-6f, "chain A output = 6");

    b.set_param("gain", 4.0f);
    b.set_input("input", a_out);
    b.process(&ctx);

    check_float(b.output("value"), 24.0f, 1e-6f, "chain B output = 24");
}

// =====================================================================
// Test 4: Persistent state — LFO accumulates phase across frames
// =====================================================================
static void test_persistent_state() {
    std::fprintf(stderr, "\n=== ChildOp Persistent State (LFO) ===\n");

    vivid::ChildOp<LFO> lfo;

    // 1 Hz sine LFO, amplitude 1, offset 0
    lfo.set_param("frequency", 1.0f);
    lfo.set_param("amplitude", 1.0f);
    lfo.set_param("offset", 0.0f);
    lfo.set_param("waveform", 0.0f);  // sine

    // Tick at t=0 with small dt
    auto ctx0 = make_ctx(0.0, 0.0001, 0);
    lfo.process(&ctx0);
    float v0 = lfo.output("value");
    // At phase ~0, sin(0) = 0
    check_float(v0, 0.0f, 0.01f, "LFO at t~0: ~0.0");

    // Tick for 250ms (quarter cycle of 1Hz) — should reach sin(pi/2) = 1.0
    auto ctx1 = make_ctx(0.25, 0.25, 1);
    lfo.process(&ctx1);
    float v1 = lfo.output("value");
    check_float(v1, 1.0f, 0.05f, "LFO at t=0.25: ~1.0 (sin peak)");

    // Tick another 250ms — half cycle, sin(pi) = 0
    auto ctx2 = make_ctx(0.5, 0.25, 2);
    lfo.process(&ctx2);
    float v2 = lfo.output("value");
    check_float(v2, 0.0f, 0.05f, "LFO at t=0.5: ~0.0 (sin zero crossing)");
}

// =====================================================================
// Test 4b: LFO metronome sync follows graph transport instead of free rate
// =====================================================================
static void test_metronome_sync() {
    std::fprintf(stderr, "\n=== ChildOp LFO Metronome Sync ===\n");

    vivid::ChildOp<LFO> lfo;
    lfo.set_param("frequency", 9.0f);      // ignored in metronome mode
    lfo.set_param("amplitude", 1.0f);
    lfo.set_param("offset", 0.0f);
    lfo.set_param("waveform", 0.0f);       // sine
    lfo.set_param("rate_mode", 2.0f);      // metronome
    lfo.set_param("sync_division", 2.0f);  // quarter notes

    auto ctx0 = make_ctx(0.0, 1.0 / 60.0, 0);
    set_metronome(ctx0, true, 120.0f, 4, 0.0, 0.0f, 0.0f);
    lfo.process(&ctx0);
    check_float(lfo.output("value"), 0.0f, 0.01f, "metronome beat 0 starts at sine zero");

    auto ctx1 = make_ctx(0.125, 1.0 / 60.0, 1);
    set_metronome(ctx1, true, 120.0f, 4, 0.25, 0.25f, 0.0625f);
    lfo.process(&ctx1);
    check_float(lfo.output("value"), 1.0f, 0.05f, "quarter-beat metronome phase reaches sine peak");

    auto ctx2 = make_ctx(0.25, 1.0 / 60.0, 2);
    set_metronome(ctx2, true, 120.0f, 4, 0.5, 0.5f, 0.125f);
    lfo.process(&ctx2);
    check_float(lfo.output("value"), 0.0f, 0.05f, "half-beat metronome phase returns to zero");
}

// =====================================================================
// Test 5: Param changes between frames
// =====================================================================
static void test_param_changes() {
    std::fprintf(stderr, "\n=== ChildOp Param Changes ===\n");

    vivid::ChildOp<ChildTestOp> child;

    child.set_param("gain", 2.0f);
    child.set_input("input", 5.0f);

    auto ctx = make_ctx(0.0, 1.0 / 60.0, 0);
    child.process(&ctx);
    check_float(child.output("value"), 10.0f, 1e-6f, "gain=2, input=5 -> 10");

    // Change gain
    child.set_param("gain", 10.0f);
    child.process(&ctx);
    check_float(child.output("value"), 50.0f, 1e-6f, "gain=10, input=5 -> 50");
}

// =====================================================================
// Test 6: Direct op() access
// =====================================================================
static void test_op_access() {
    std::fprintf(stderr, "\n=== ChildOp Direct op() Access ===\n");

    vivid::ChildOp<LFO> lfo;

    // Access internal state directly
    check(lfo.op().scalar_state_.free_phase == 0.0, "LFO initial phase is 0");

    // Process a frame
    lfo.set_param("frequency", 1.0f);
    lfo.set_param("amplitude", 1.0f);
    lfo.set_param("offset", 0.0f);
    lfo.set_param("waveform", 0.0f);

    auto ctx = make_ctx(0.0, 0.1, 0);
    lfo.process(&ctx);

    check(lfo.op().scalar_state_.free_phase > 0.0, "LFO phase advanced after process()");
}

// =====================================================================
// Test 7: Smooth operator — exercises time-dependent state
// =====================================================================
static void test_smooth_child() {
    std::fprintf(stderr, "\n=== ChildOp Smooth Operator ===\n");

    vivid::ChildOp<Smooth> smooth;
    smooth.set_param("rise_time", 0.1f);
    smooth.set_param("fall_time", 0.1f);

    // First frame: snaps to input
    smooth.set_input("input", 1.0f);
    auto ctx0 = make_ctx(0.0, 1.0 / 60.0, 0);
    smooth.process(&ctx0);
    check_float(smooth.output("value"), 1.0f, 0.01f, "Smooth first frame snaps to input");

    // Step to 0 — should start smoothing toward 0
    smooth.set_input("input", 0.0f);
    auto ctx1 = make_ctx(1.0 / 60.0, 1.0 / 60.0, 1);
    smooth.process(&ctx1);
    float v1 = smooth.output("value");
    check(v1 > 0.0f && v1 < 1.0f, "Smooth is between 0 and 1 after one frame");

    // After many frames, should converge near 0
    for (int i = 2; i < 200; ++i) {
        double t = i / 60.0;
        auto ctx = make_ctx(t, 1.0 / 60.0, static_cast<uint64_t>(i));
        smooth.process(&ctx);
    }
    check_float(smooth.output("value"), 0.0f, 0.01f, "Smooth converged near 0 after many frames");
}

// =====================================================================
int main() {
    test_basic();
    test_index_access();
    test_chaining();
    test_persistent_state();
    test_metronome_sync();
    test_param_changes();
    test_op_access();
    test_smooth_child();

    std::fprintf(stderr, "\n%s (%d failure%s)\n",
                 failures ? "FAILED" : "ALL TESTS PASSED",
                 failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
