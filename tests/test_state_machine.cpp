#include "operator_api/operator.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

// Include the operator source directly so we can instantiate it
#include "../operators/control/state_machine/state_machine.cpp"

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_float(float actual, float expected, const char* msg, float tol = 1e-4f) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "  FAIL: %s (expected %f, got %f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%f)\n", msg, actual);
    }
}

// Helper to drive the state machine operator directly
struct SMDriver {
    StateMachine sm;
    std::vector<vivid::ParamBase*> param_ptrs;
    std::vector<float> param_values;
    float input_values[4] = {};   // beat_phase, trigger, reset, signal
    float output_values[5] = {};  // state, progress, trigger, bar, beat
    uint64_t frame = 0;

    SMDriver() {
        sm.collect_params(param_ptrs);
        param_values.resize(param_ptrs.size());
        for (size_t i = 0; i < param_ptrs.size(); ++i) {
            param_values[i] = param_ptrs[i]->default_value;
        }
    }

    void set_param(const char* name, float value) {
        for (size_t i = 0; i < param_ptrs.size(); ++i) {
            if (std::strcmp(param_ptrs[i]->name, name) == 0) {
                param_values[i] = value;
                return;
            }
        }
    }

    void tick() {
        VividProcessContext ctx{};
        ctx.param_values = param_values.data();
        ctx.input_values = input_values;
        ctx.output_values = output_values;
        ctx.time = 0.0;
        ctx.delta_time = 0.016;
        ctx.frame = frame++;
        sm.process(&ctx);
    }

    float state()    const { return output_values[0]; }
    float progress() const { return output_values[1]; }
    float trigger()  const { return output_values[2]; }
    float bar()      const { return output_values[3]; }
    float beat()     const { return output_values[4]; }

    // Simulate N complete beat wraps (phase goes from near 1 to near 0)
    void simulate_beats(int num_beats) {
        for (int b = 0; b < num_beats; ++b) {
            input_values[0] = 0.9f;  // near end of beat
            tick();
            input_values[0] = 0.1f;  // wrap to start (delta = 0.1 - 0.9 = -0.8 < -0.5)
            tick();
        }
    }
};

int main() {
    std::fprintf(stderr, "\n=== Test: StateMachine Operator ===\n");

    // ---------------------------------------------------------------
    // Test 1: Sequential mode — 4 states, 2 bars each, 4 beats/bar
    // ---------------------------------------------------------------
    std::fprintf(stderr, "\n--- Sequential mode ---\n");
    {
        SMDriver d;
        d.set_param("states", 4);
        d.set_param("transition", 0);  // sequential
        d.set_param("quantize", 0);    // off
        d.set_param("loop", 1);
        d.set_param("bars_per_beat", 4);
        d.set_param("dur_0", 2);
        d.set_param("dur_1", 2);
        d.set_param("dur_2", 2);
        d.set_param("dur_3", 2);

        d.input_values[0] = 0.0f;
        d.tick();
        check_float(d.state(), 0.0f, "initial state = 0");
        check_float(d.bar(), 0.0f, "initial bar = 0");

        // 4 beats/bar * 2 bars = 8 beats to advance one state
        d.simulate_beats(8);
        check_float(d.state(), 1.0f, "after 8 beats: state = 1");
        check_float(d.bar(), 0.0f, "bar resets to 0");

        d.simulate_beats(8);
        check_float(d.state(), 2.0f, "after 16 beats: state = 2");

        d.simulate_beats(8);
        check_float(d.state(), 3.0f, "after 24 beats: state = 3");

        // Loops back to 0
        d.simulate_beats(8);
        check_float(d.state(), 0.0f, "after 32 beats: loops to state = 0");
    }

    // ---------------------------------------------------------------
    // Test 2: Manual mode — trigger advances state
    // ---------------------------------------------------------------
    std::fprintf(stderr, "\n--- Manual mode ---\n");
    {
        SMDriver d;
        d.set_param("states", 3);
        d.set_param("transition", 1);  // manual
        d.set_param("quantize", 0);
        d.set_param("loop", 1);

        d.input_values[0] = 0.0f;
        d.tick();
        check_float(d.state(), 0.0f, "manual: initial state = 0");

        // Rising edge on trigger
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 1.0f, "manual: trigger -> state = 1");
        check_float(d.trigger(), 1.0f, "manual: trigger output fires");

        // Held high — no advance
        d.tick();
        check_float(d.state(), 1.0f, "manual: held trigger stays 1");
        check_float(d.trigger(), 0.0f, "manual: trigger output clears");

        // Release and re-trigger
        d.input_values[1] = 0.0f;
        d.tick();
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 2.0f, "manual: re-trigger -> state = 2");

        // Loops to 0
        d.input_values[1] = 0.0f;
        d.tick();
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 0.0f, "manual: loops to state = 0");
    }

    // ---------------------------------------------------------------
    // Test 3: Threshold mode — signal crossing advances state
    // ---------------------------------------------------------------
    std::fprintf(stderr, "\n--- Threshold mode ---\n");
    {
        SMDriver d;
        d.set_param("states", 3);
        d.set_param("transition", 2);  // threshold
        d.set_param("quantize", 0);
        d.set_param("loop", 1);
        d.set_param("threshold", 0.5f);

        d.input_values[0] = 0.0f;
        d.input_values[3] = 0.0f;  // signal
        d.tick();
        check_float(d.state(), 0.0f, "threshold: initial state = 0");

        // Signal crosses 0.5 upward
        d.input_values[3] = 0.7f;
        d.tick();
        check_float(d.state(), 1.0f, "threshold: upward cross -> state = 1");

        // Signal crosses 0.5 downward
        d.input_values[3] = 0.3f;
        d.tick();
        check_float(d.state(), 2.0f, "threshold: downward cross -> state = 2");
    }

    // ---------------------------------------------------------------
    // Test 4: Loop off — stops at last state
    // ---------------------------------------------------------------
    std::fprintf(stderr, "\n--- Loop off ---\n");
    {
        SMDriver d;
        d.set_param("states", 2);
        d.set_param("transition", 1);  // manual
        d.set_param("quantize", 0);
        d.set_param("loop", 0);

        d.input_values[0] = 0.0f;
        d.tick();

        // Advance to state 1
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 1.0f, "no-loop: state = 1");

        // Try to advance past last — should stay at 1
        d.input_values[1] = 0.0f;
        d.tick();
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 1.0f, "no-loop: stays at 1 (finished)");
    }

    // ---------------------------------------------------------------
    // Test 5: Duration-0 override — holds until manual trigger
    // ---------------------------------------------------------------
    std::fprintf(stderr, "\n--- Duration-0 override ---\n");
    {
        SMDriver d;
        d.set_param("states", 3);
        d.set_param("transition", 0);  // sequential
        d.set_param("quantize", 0);
        d.set_param("loop", 1);
        d.set_param("bars_per_beat", 4);
        d.set_param("dur_0", 0);   // infinite hold
        d.set_param("dur_1", 2);
        d.set_param("dur_2", 2);

        d.input_values[0] = 0.0f;
        d.tick();

        // Many beats — state 0 should hold (dur=0)
        d.simulate_beats(20);
        check_float(d.state(), 0.0f, "dur-0: stays at state 0 after 20 beats");

        // Manual trigger advances even in sequential mode
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 1.0f, "dur-0: manual trigger -> state = 1");
    }

    // ---------------------------------------------------------------
    // Test 6: Reset input
    // ---------------------------------------------------------------
    std::fprintf(stderr, "\n--- Reset ---\n");
    {
        SMDriver d;
        d.set_param("states", 4);
        d.set_param("transition", 1);  // manual
        d.set_param("quantize", 0);
        d.set_param("loop", 1);

        d.input_values[0] = 0.0f;
        d.tick();

        // Advance to state 2
        d.input_values[1] = 1.0f;
        d.tick();
        d.input_values[1] = 0.0f;
        d.tick();
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 2.0f, "reset: at state 2");

        // Reset
        d.input_values[1] = 0.0f;
        d.input_values[2] = 1.0f;  // reset
        d.tick();
        check_float(d.state(), 0.0f, "reset: back to state 0");
        check_float(d.bar(), 0.0f, "reset: bar = 0");
    }

    // ---------------------------------------------------------------
    // Test 7: Quantized transitions — deferred to bar boundary
    // ---------------------------------------------------------------
    std::fprintf(stderr, "\n--- Quantized transitions ---\n");
    {
        SMDriver d;
        d.set_param("states", 3);
        d.set_param("transition", 1);  // manual
        d.set_param("quantize", 1);    // on
        d.set_param("loop", 1);
        d.set_param("bars_per_beat", 2);  // 2 beats per bar

        d.input_values[0] = 0.0f;
        d.tick();

        // Trigger mid-bar
        d.input_values[0] = 0.5f;
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 0.0f, "quant: trigger mid-bar stays 0 (deferred)");

        // Release trigger
        d.input_values[1] = 0.0f;
        d.tick();

        // Simulate 2 beat wraps to cross a bar boundary
        d.simulate_beats(2);
        check_float(d.state(), 1.0f, "quant: advances at bar boundary -> state = 1");
    }

    // ---------------------------------------------------------------
    // Test 8: Progress output
    // ---------------------------------------------------------------
    std::fprintf(stderr, "\n--- Progress output ---\n");
    {
        SMDriver d;
        d.set_param("states", 2);
        d.set_param("transition", 0);  // sequential
        d.set_param("quantize", 0);
        d.set_param("loop", 1);
        d.set_param("bars_per_beat", 2);  // 2 beats per bar
        d.set_param("dur_0", 4);  // 4 bars per state
        d.set_param("dur_1", 4);

        d.input_values[0] = 0.0f;
        d.tick();
        check_float(d.progress(), 0.0f, "progress: starts at 0", 0.05f);

        // 2 beats = 1 bar, 4 bars total → each bar is 25%
        d.simulate_beats(2);
        check_float(d.progress(), 0.25f, "progress: 1 bar = 0.25", 0.1f);

        d.simulate_beats(2);
        check_float(d.progress(), 0.5f, "progress: 2 bars = 0.5", 0.1f);
    }

    // ---------------------------------------------------------------
    // Test 9: Duration-0 progress should be 0
    // ---------------------------------------------------------------
    std::fprintf(stderr, "\n--- Duration-0 progress ---\n");
    {
        SMDriver d;
        d.set_param("states", 2);
        d.set_param("transition", 0);
        d.set_param("quantize", 0);
        d.set_param("loop", 1);
        d.set_param("bars_per_beat", 4);
        d.set_param("dur_0", 0);
        d.set_param("dur_1", 4);

        d.input_values[0] = 0.0f;
        d.tick();

        d.simulate_beats(10);
        check_float(d.progress(), 0.0f, "dur-0 progress stays 0");
    }

    // ---------------------------------------------------------------
    // Test 10: Trigger output fires exactly on transition frame
    // ---------------------------------------------------------------
    std::fprintf(stderr, "\n--- Trigger output ---\n");
    {
        SMDriver d;
        d.set_param("states", 2);
        d.set_param("transition", 1);  // manual
        d.set_param("quantize", 0);
        d.set_param("loop", 1);

        d.input_values[0] = 0.0f;
        d.tick();
        check_float(d.trigger(), 0.0f, "trigger: 0 initially");

        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.trigger(), 1.0f, "trigger: 1 on transition frame");

        d.tick();
        check_float(d.trigger(), 0.0f, "trigger: 0 on next frame");
    }

    // ---------------------------------------------------------------
    // Test 11: Beat output is passthrough of beat_phase
    // ---------------------------------------------------------------
    std::fprintf(stderr, "\n--- Beat output ---\n");
    {
        SMDriver d;
        d.input_values[0] = 0.42f;
        d.tick();
        check_float(d.beat(), 0.42f, "beat output = beat_phase passthrough");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
