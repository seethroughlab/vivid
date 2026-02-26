#include "operator_api/operator.h"
#include <cmath>

struct NotePattern : vivid::OperatorBase {
    static constexpr const char* kName   = "NotePattern";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   steps        {"steps",          4, 1, 8};
    vivid::Param<int>   root_0       {"root_0",         0, 0, 11};
    vivid::Param<int>   root_1       {"root_1",         0, 0, 11};
    vivid::Param<int>   root_2       {"root_2",         0, 0, 11};
    vivid::Param<int>   root_3       {"root_3",         0, 0, 11};
    vivid::Param<int>   root_4       {"root_4",         0, 0, 11};
    vivid::Param<int>   root_5       {"root_5",         0, 0, 11};
    vivid::Param<int>   root_6       {"root_6",         0, 0, 11};
    vivid::Param<int>   root_7       {"root_7",         0, 0, 11};
    vivid::Param<int>   type_0       {"type_0",         0, 0, 6};
    vivid::Param<int>   type_1       {"type_1",         0, 0, 6};
    vivid::Param<int>   type_2       {"type_2",         0, 0, 6};
    vivid::Param<int>   type_3       {"type_3",         0, 0, 6};
    vivid::Param<int>   type_4       {"type_4",         0, 0, 6};
    vivid::Param<int>   type_5       {"type_5",         0, 0, 6};
    vivid::Param<int>   type_6       {"type_6",         0, 0, 6};
    vivid::Param<int>   type_7       {"type_7",         0, 0, 6};
    vivid::Param<int>   octave       {"octave",         4, 2, 7};
    vivid::Param<int>   beats_per_step{"beats_per_step", 4, 1, 16};
    vivid::Param<float> gate_length  {"gate_length",    0.8f, 0.01f, 1.0f};
    vivid::Param<float> velocity     {"velocity",       0.8f, 0.0f, 1.0f};

    // Internal state
    int beat_count_ = 0;
    float prev_phase_ = 0.0f;

    // Chord interval tables: [type][interval_count], terminated by -1
    // 0=major, 1=minor, 2=dim, 3=aug, 4=dom7, 5=min7, 6=maj7
    static constexpr int kChordIntervals[7][5] = {
        {0, 4, 7, -1, -1},    // major
        {0, 3, 7, -1, -1},    // minor
        {0, 3, 6, -1, -1},    // dim
        {0, 4, 8, -1, -1},    // aug
        {0, 4, 7, 10, -1},    // dom7
        {0, 3, 7, 10, -1},    // min7
        {0, 4, 7, 11, -1},    // maj7
    };

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&steps);
        out.push_back(&root_0); out.push_back(&root_1);
        out.push_back(&root_2); out.push_back(&root_3);
        out.push_back(&root_4); out.push_back(&root_5);
        out.push_back(&root_6); out.push_back(&root_7);
        out.push_back(&type_0); out.push_back(&type_1);
        out.push_back(&type_2); out.push_back(&type_3);
        out.push_back(&type_4); out.push_back(&type_5);
        out.push_back(&type_6); out.push_back(&type_7);
        out.push_back(&octave);
        out.push_back(&beats_per_step);
        out.push_back(&gate_length);
        out.push_back(&velocity);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"notes",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});
        out.push_back({"velocities", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});
        out.push_back({"gates",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        float beat_phase = ctx->input_values[0];
        int num_steps = steps.int_value();
        int oct = octave.int_value();
        int bps = beats_per_step.int_value();
        float gl = gate_length.value;
        float vel = velocity.value;

        // Detect beat_phase wraps (delta < -0.5) → increment beat_count_
        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) {
            beat_count_++;
        }
        prev_phase_ = beat_phase;

        // Current step
        int current_step = (beat_count_ / bps) % num_steps;

        // Look up root and chord type for current step
        // Params are ordered: steps, root_0..root_7, type_0..type_7, octave, beats_per_step, ...
        // root_0 is param index 1, type_0 is param index 9
        int root = static_cast<int>(ctx->param_values[1 + current_step]);
        int chord_type = static_cast<int>(ctx->param_values[9 + current_step]);
        if (chord_type < 0) chord_type = 0;
        if (chord_type > 6) chord_type = 6;

        // Count chord intervals
        const int* intervals = kChordIntervals[chord_type];
        int chord_size = 0;
        for (int i = 0; i < 5 && intervals[i] >= 0; ++i) {
            chord_size++;
        }

        // Gate: 1.0 if beat_phase < gate_length, else 0.0
        float gate_val = (beat_phase < gl) ? 1.0f : 0.0f;

        // Write output spreads
        if (ctx->output_spreads) {
            auto& notes_sp = ctx->output_spreads[0];
            auto& vel_sp   = ctx->output_spreads[1];
            auto& gates_sp = ctx->output_spreads[2];

            uint32_t len = static_cast<uint32_t>(chord_size);
            if (notes_sp.capacity >= len) {
                notes_sp.length = len;
                vel_sp.length   = len;
                gates_sp.length = len;
                for (uint32_t i = 0; i < len; ++i) {
                    notes_sp.data[i] = static_cast<float>(root + oct * 12 + intervals[i]);
                    vel_sp.data[i]   = vel;
                    gates_sp.data[i] = gate_val;
                }
            }
        }

        // Scalar fallback: first note
        if (chord_size > 0) {
            ctx->output_values[0] = static_cast<float>(root + oct * 12 + intervals[0]);
            ctx->output_values[1] = vel;
            ctx->output_values[2] = gate_val;
        }
    }
};

VIVID_REGISTER(NotePattern)
