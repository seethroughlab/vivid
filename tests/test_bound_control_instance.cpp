#include "operator_api/bound_control_instance.h"
#include "control/envelope/envelope.h"
#include "control/lfo/lfo.h"
#include <cstdio>
#include <cmath>
#include <memory>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_float(float actual, float expected, float tol, const char* msg) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "  FAIL: %s (expected %f, got %f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%f)\n", msg, actual);
    }
}

static VividProcessContext make_ctx(double time, double dt, uint64_t frame) {
    VividProcessContext ctx{};
    ctx.time       = time;
    ctx.delta_time = dt;
    ctx.frame      = frame;
    return ctx;
}

// =====================================================================
// Test 1: Introspection — verify Envelope param/port counts and names
// =====================================================================
static void test_introspection() {
    std::fprintf(stderr, "\n=== BoundControlInstance Introspection ===\n");

    vivid::BoundControlInstance slot(std::make_unique<Envelope>());

    check(slot.param_count() == 7, "Envelope has 7 params");
    check(slot.input_count() == 2, "Envelope has 2 inputs (gate, beat_phase)");
    check(slot.output_count() == 1, "Envelope has 1 output (value)");

    check(slot.has_param("attack"), "has param 'attack'");
    check(slot.has_param("decay"), "has param 'decay'");
    check(slot.has_param("sustain"), "has param 'sustain'");
    check(slot.has_param("release"), "has param 'release'");
    check(slot.has_param("amplitude"), "has param 'amplitude'");
    check(slot.has_param("offset"), "has param 'offset'");
    check(!slot.has_param("nonexistent"), "does not have param 'nonexistent'");

    check(slot.has_input("gate"), "has input 'gate'");
    check(slot.has_input("beat_phase"), "has input 'beat_phase'");
    check(!slot.has_input("nonexistent"), "does not have input 'nonexistent'");

    check(slot.has_output("value"), "has output 'value'");
    check(!slot.has_output("nonexistent"), "does not have output 'nonexistent'");
}

// =====================================================================
// Test 2: Param set/get by name
// =====================================================================
static void test_param_access() {
    std::fprintf(stderr, "\n=== BoundControlInstance Param Access ===\n");

    vivid::BoundControlInstance slot(std::make_unique<Envelope>());

    slot.set_param("attack", 0.1f);
    check_float(slot.param("attack"), 0.1f, 1e-6f, "attack set to 0.1");

    slot.set_param("sustain", 0.5f);
    check_float(slot.param("sustain"), 0.5f, 1e-6f, "sustain set to 0.5");
}

// =====================================================================
// Test 3: Input/output by name
// =====================================================================
static void test_input_output() {
    std::fprintf(stderr, "\n=== BoundControlInstance Input/Output ===\n");

    vivid::BoundControlInstance slot(std::make_unique<Envelope>());

    slot.set_input("gate", 1.0f);
    auto ctx = make_ctx(0.0, 0.01, 0);
    slot.process(&ctx);

    float val = slot.output("value");
    check(val > 0.0f, "output > 0 after gate on");
}

// =====================================================================
// Test 4: LFO basic — verify output changes over time
// =====================================================================
static void test_lfo_basic() {
    std::fprintf(stderr, "\n=== BoundControlInstance LFO Basic ===\n");

    vivid::BoundControlInstance slot(std::make_unique<LFO>());

    slot.set_param("frequency", 1.0f);
    slot.set_param("amplitude", 1.0f);
    slot.set_param("offset", 0.0f);
    slot.set_param("waveform", 0.0f);  // sine

    // Process several frames and verify output changes
    auto ctx0 = make_ctx(0.0, 0.01, 0);
    slot.process(&ctx0);
    float v0 = slot.output("value");

    auto ctx1 = make_ctx(0.01, 0.01, 1);
    slot.process(&ctx1);
    float v1 = slot.output("value");

    auto ctx2 = make_ctx(0.02, 0.01, 2);
    slot.process(&ctx2);
    float v2 = slot.output("value");

    // At 1Hz sine, values should be increasing from 0
    check(v1 > v0, "LFO output increasing from t=0");
    check(v2 > v1, "LFO output still increasing at t=0.02");

    // Process to quarter cycle — should be near peak
    double t = 0.03;
    for (uint64_t i = 3; i < 25; ++i) {
        auto ctx = make_ctx(t, 0.01, i);
        slot.process(&ctx);
        t += 0.01;
    }
    float v_peak = slot.output("value");
    check_float(v_peak, 1.0f, 0.1f, "LFO near peak at ~quarter cycle");
}

// =====================================================================
// Test 5: Envelope gate lifecycle
// =====================================================================
static void test_envelope_lifecycle() {
    std::fprintf(stderr, "\n=== BoundControlInstance Envelope Lifecycle ===\n");

    vivid::BoundControlInstance slot(std::make_unique<Envelope>());

    // Set known ADSR: attack=0.1s, decay=0.2s, sustain=0.7, release=0.3s
    slot.set_param("attack", 0.1f);
    slot.set_param("decay", 0.2f);
    slot.set_param("sustain", 0.7f);
    slot.set_param("release", 0.3f);
    slot.set_param("amplitude", 1.0f);
    slot.set_param("offset", 0.0f);

    double dt = 0.01;
    double t = 0.0;
    uint64_t frame = 0;

    // Gate on
    slot.set_input("gate", 1.0f);

    // Process through attack phase (0.1s = 10 frames at dt=0.01)
    for (int i = 0; i < 12; ++i) {
        auto ctx = make_ctx(t, dt, frame++);
        slot.process(&ctx);
        t += dt;
    }
    float after_attack = slot.output("value");
    check(after_attack > 0.9f, "near peak after attack phase");

    // Process through decay to sustain (~0.2s more)
    for (int i = 0; i < 40; ++i) {
        auto ctx = make_ctx(t, dt, frame++);
        slot.process(&ctx);
        t += dt;
    }
    float at_sustain = slot.output("value");
    check_float(at_sustain, 0.7f, 0.05f, "at sustain level");

    // Sustain holds
    for (int i = 0; i < 10; ++i) {
        auto ctx = make_ctx(t, dt, frame++);
        slot.process(&ctx);
        t += dt;
    }
    float sustain_hold = slot.output("value");
    check_float(sustain_hold, 0.7f, 0.05f, "sustain holds steady");

    // Gate off — release
    slot.set_input("gate", 0.0f);
    for (int i = 0; i < 50; ++i) {
        auto ctx = make_ctx(t, dt, frame++);
        slot.process(&ctx);
        t += dt;
    }
    float after_release = slot.output("value");
    check(after_release < 0.05f, "near zero after release");
}

// =====================================================================
// Test 6: apply_template()
// =====================================================================
static void test_apply_template() {
    std::fprintf(stderr, "\n=== BoundControlInstance apply_template ===\n");

    vivid::BoundControlInstance slot(std::make_unique<Envelope>());

    // Verify defaults
    check_float(slot.param("attack"), 0.001f, 1e-6f, "default attack = 0.001");
    check_float(slot.param("sustain"), 0.7f, 1e-6f, "default sustain = 0.7");

    // Apply template
    slot.apply_template({{"attack", 0.5f}, {"sustain", 0.3f}});

    check_float(slot.param("attack"), 0.5f, 1e-6f, "attack changed to 0.5");
    check_float(slot.param("sustain"), 0.3f, 1e-6f, "sustain changed to 0.3");

    // Other params unchanged
    check_float(slot.param("decay"), 0.2f, 1e-6f, "decay still default 0.2");
}

// =====================================================================
// Test 7: reset() with factory
// =====================================================================
static void test_reset() {
    std::fprintf(stderr, "\n=== BoundControlInstance reset ===\n");

    auto factory = []() -> std::unique_ptr<vivid::OperatorBase> {
        return std::make_unique<Envelope>();
    };

    vivid::BoundControlInstance slot(factory(), factory);

    // Set params and process through attack
    slot.set_param("attack", 0.1f);
    slot.set_param("amplitude", 1.0f);
    slot.set_input("gate", 1.0f);

    double t = 0.0;
    double dt = 0.01;
    for (uint64_t i = 0; i < 15; ++i) {
        auto ctx = make_ctx(t, dt, i);
        slot.process(&ctx);
        t += dt;
    }
    float before_reset = slot.output("value");
    check(before_reset > 0.5f, "output > 0.5 before reset");

    // Reset
    slot.reset();

    // After reset, output should be 0 (fresh operator, gate not set)
    auto ctx = make_ctx(0.0, dt, 0);
    slot.process(&ctx);
    float after_reset = slot.output("value");
    check_float(after_reset, 0.0f, 1e-6f, "output = 0 after reset (idle)");

    // Params should be back to defaults
    check_float(slot.param("attack"), 0.001f, 1e-6f, "attack back to default after reset");
}

// =====================================================================
int main() {
    test_introspection();
    test_param_access();
    test_input_output();
    test_lfo_basic();
    test_envelope_lifecycle();
    test_apply_template();
    test_reset();

    std::fprintf(stderr, "\n%s (%d failure%s)\n",
                 failures ? "FAILED" : "ALL TESTS PASSED",
                 failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
