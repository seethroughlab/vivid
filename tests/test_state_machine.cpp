#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

#include "operator_api/types.h"

// Include the operator source directly so we can instantiate it.
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

struct SMDriver {
    StateMachine sm;
    std::vector<vivid::ParamBase*> param_ptrs;
    std::vector<float> param_values;
    float input_values[4] = {};
    float output_values[6] = {};
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
        VividAudioContext ctx{};
        ctx.time = 0.0;
        ctx.delta_time = 0.016;
        ctx.frame = frame++;
        ctx.param_values = param_values.data();
        ctx.input_float_values = input_values;
        ctx.output_float_values = output_values;
        ctx.buffer_size = 0;
        ctx.sample_rate = 48000;
        sm.process_audio(&ctx);
    }

    float state() const { return output_values[0]; }
    float progress() const { return output_values[1]; }
    float trigger() const { return output_values[2]; }
    float bar() const { return output_values[3]; }
    float beat() const { return output_values[4]; }
    float xfade() const { return output_values[5]; }

    void simulate_beats(int num_beats) {
        for (int b = 0; b < num_beats; ++b) {
            input_values[0] = 0.9f;
            tick();
            input_values[0] = 0.1f;
            tick();
        }
    }
};

int main() {
    std::fprintf(stderr, "\n=== Test: StateMachine Operator ===\n");

    std::fprintf(stderr, "\n--- Sequential mode ---\n");
    {
        SMDriver d;
        d.set_param("states", 4);
        d.set_param("transition", 0);
        d.set_param("quantize", 0);
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

        d.simulate_beats(8);
        check_float(d.state(), 1.0f, "after 8 beats: state = 1");
        check_float(d.bar(), 0.0f, "bar resets to 0");

        d.simulate_beats(8);
        check_float(d.state(), 2.0f, "after 16 beats: state = 2");

        d.simulate_beats(8);
        check_float(d.state(), 3.0f, "after 24 beats: state = 3");

        d.simulate_beats(8);
        check_float(d.state(), 0.0f, "after 32 beats: loops to state = 0");
    }

    std::fprintf(stderr, "\n--- Manual mode ---\n");
    {
        SMDriver d;
        d.set_param("states", 3);
        d.set_param("transition", 1);
        d.set_param("quantize", 0);
        d.set_param("loop", 1);

        d.input_values[0] = 0.0f;
        d.tick();
        check_float(d.state(), 0.0f, "manual: initial state = 0");

        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 1.0f, "manual: trigger -> state = 1");
        check_float(d.trigger(), 1.0f, "manual: trigger output fires");

        d.tick();
        check_float(d.state(), 1.0f, "manual: held trigger stays 1");
        check_float(d.trigger(), 0.0f, "manual: trigger output clears");

        d.input_values[1] = 0.0f;
        d.tick();
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 2.0f, "manual: re-trigger -> state = 2");

        d.input_values[1] = 0.0f;
        d.tick();
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 0.0f, "manual: loops to state = 0");
    }

    std::fprintf(stderr, "\n--- Threshold mode ---\n");
    {
        SMDriver d;
        d.set_param("states", 3);
        d.set_param("transition", 2);
        d.set_param("quantize", 0);
        d.set_param("loop", 1);
        d.set_param("threshold", 0.5f);

        d.input_values[0] = 0.0f;
        d.input_values[3] = 0.0f;
        d.tick();
        check_float(d.state(), 0.0f, "threshold: initial state = 0");

        d.input_values[3] = 0.7f;
        d.tick();
        check_float(d.state(), 1.0f, "threshold: upward cross -> state = 1");

        d.input_values[3] = 0.3f;
        d.tick();
        check_float(d.state(), 2.0f, "threshold: downward cross -> state = 2");
    }

    std::fprintf(stderr, "\n--- Loop off ---\n");
    {
        SMDriver d;
        d.set_param("states", 2);
        d.set_param("transition", 1);
        d.set_param("quantize", 0);
        d.set_param("loop", 0);

        d.input_values[0] = 0.0f;
        d.tick();

        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 1.0f, "no-loop: state = 1");

        d.input_values[1] = 0.0f;
        d.tick();
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 1.0f, "no-loop: stays at 1 (finished)");
    }

    std::fprintf(stderr, "\n--- Quantized advance ---\n");
    {
        SMDriver d;
        d.set_param("states", 3);
        d.set_param("transition", 1);
        d.set_param("quantize", 1);
        d.set_param("bars_per_beat", 4);

        d.input_values[0] = 0.2f;
        d.tick();
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 0.0f, "quantized: stays pending before bar boundary");

        d.input_values[1] = 0.0f;
        d.input_values[0] = 0.9f;
        d.tick();
        d.input_values[0] = 0.1f;
        d.tick();
        d.simulate_beats(3);
        check_float(d.state(), 1.0f, "quantized: advances on next bar");
    }

    std::fprintf(stderr, "\n--- Reset behavior ---\n");
    {
        SMDriver d;
        d.set_param("states", 4);
        d.set_param("transition", 1);
        d.set_param("quantize", 0);

        d.input_values[1] = 1.0f;
        d.tick();
        d.input_values[1] = 0.0f;
        d.tick();
        d.input_values[1] = 1.0f;
        d.tick();
        check_float(d.state(), 2.0f, "pre-reset: state = 2");

        d.input_values[1] = 0.0f;
        d.input_values[2] = 1.0f;
        d.tick();
        check_float(d.state(), 0.0f, "reset -> state = 0");
        check_float(d.bar(), 0.0f, "reset clears bar");
        check_float(d.beat(), 0.0f, "reset clears beat");
    }

    std::fprintf(stderr, "\n--- Crossfade ---\n");
    {
        SMDriver d;
        d.set_param("states", 2);
        d.set_param("transition", 1);
        d.set_param("quantize", 0);
        d.set_param("xfade_mode", 1);
        d.set_param("xfade_bars", 1.0f);
        d.set_param("bars_per_beat", 4);

        d.input_values[1] = 1.0f;
        d.tick();
        check(d.xfade() >= 0.0f && d.xfade() <= 1.0f, "xfade starts in [0,1]");

        d.input_values[1] = 0.0f;
        d.simulate_beats(4);
        check(d.xfade() > 0.9f, "xfade approaches 1 after one bar");
    }

    if (failures > 0) {
        std::fprintf(stderr, "\n%d test(s) failed\n", failures);
        return 1;
    }

    std::fprintf(stderr, "\nAll StateMachine tests passed\n");
    return 0;
}
