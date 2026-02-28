#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

struct PatternSeq : vivid::OperatorBase {
    static constexpr const char* kName   = "PatternSeq";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   steps       {"steps",       8, 1, 16};
    vivid::Param<int>   rate        {"rate",        2, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T"}};
    vivid::Param<float> gate_length {"gate_length", 0.8f, 0.01f, 1.0f};
    vivid::Param<float> probability {"probability", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> val_0  {"val_0",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_1  {"val_1",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_2  {"val_2",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_3  {"val_3",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_4  {"val_4",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_5  {"val_5",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_6  {"val_6",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_7  {"val_7",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_8  {"val_8",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_9  {"val_9",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_10 {"val_10", 0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_11 {"val_11", 0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_12 {"val_12", 0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_13 {"val_13", 0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_14 {"val_14", 0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_15 {"val_15", 0.0f, -10000.0f, 10000.0f};

    // Param indices: steps=0, rate=1, gate_length=2, probability=3, val_0..val_15=4..19

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&steps);        // 0
        out.push_back(&rate);         // 1
        out.push_back(&gate_length);  // 2
        out.push_back(&probability);  // 3
        out.push_back(&val_0);  out.push_back(&val_1);   // 4..5
        out.push_back(&val_2);  out.push_back(&val_3);   // 6..7
        out.push_back(&val_4);  out.push_back(&val_5);   // 8..9
        out.push_back(&val_6);  out.push_back(&val_7);   // 10..11
        out.push_back(&val_8);  out.push_back(&val_9);   // 12..13
        out.push_back(&val_10); out.push_back(&val_11);  // 14..15
        out.push_back(&val_12); out.push_back(&val_13);  // 16..17
        out.push_back(&val_14); out.push_back(&val_15);  // 18..19
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_INPUT});   // in[0]
        out.push_back({"value",      VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});   // out[0]
        out.push_back({"trigger",    VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});   // out[1]
        out.push_back({"gate",       VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});   // out[2]
        out.push_back({"step",       VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});   // out[3]
        out.push_back({"pattern",    VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});   // spread[0]
    }

    void process(const VividProcessContext* ctx) override {
        float beat_phase = ctx->input_values[0];
        int n   = std::clamp(static_cast<int>(ctx->param_values[0]), 1, 16);
        int r   = std::clamp(static_cast<int>(ctx->param_values[1]), 0, 8);
        float gl = ctx->param_values[2];
        float prob = ctx->param_values[3];

        // Beat tracking
        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) beat_count_++;
        prev_phase_ = beat_phase;

        // Rate multiplier and current step
        float multiplier = kMultipliers[r];
        float total_beats = static_cast<float>(beat_count_) + beat_phase;
        float scaled_phase = total_beats * multiplier;

        int global_step = static_cast<int>(std::floor(scaled_phase));
        int current_step = ((global_step % n) + n) % n;
        float step_phase = scaled_phase - std::floor(scaled_phase);

        // Step value (val_0 is param index 4)
        float value = ctx->param_values[4 + current_step];

        // Trigger on step change
        bool new_step = (current_step != prev_step_);
        prev_step_ = current_step;

        // Probability: deterministic per (beat_count cycle, step)
        // Hash of global_step ensures same step repeats consistently within a pattern,
        // but varies across cycles
        bool fires = true;
        if (prob < 1.0f) {
            uint32_t seed = static_cast<uint32_t>(global_step);
            seed = xorshift32(seed == 0 ? 1 : seed);
            float rand01 = static_cast<float>(seed) / 4294967295.0f;
            fires = (rand01 < prob);
        }

        float out_value = fires ? value : 0.0f;
        float trigger = (new_step && fires) ? 1.0f : 0.0f;
        float gate = (fires && step_phase < gl) ? 1.0f : 0.0f;

        ctx->output_values[0] = out_value;
        ctx->output_values[1] = trigger;
        ctx->output_values[2] = gate;
        ctx->output_values[3] = static_cast<float>(current_step);

        // Full pattern as spread (output port 4 = pattern)
        if (ctx->output_spreads) {
            auto& sp = ctx->output_spreads[4];
            auto len = static_cast<uint32_t>(n);
            if (sp.capacity >= len) {
                sp.length = len;
                for (uint32_t i = 0; i < len; ++i)
                    sp.data[i] = ctx->param_values[4 + i];
            }
        }
    }

private:
    static constexpr float kMultipliers[] = {
        0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 1.5f, 3.0f, 6.0f
    };

    float prev_phase_ = 0.0f;
    int beat_count_ = 0;
    int prev_step_ = -1;

    static uint32_t xorshift32(uint32_t state) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};

VIVID_REGISTER(PatternSeq)
