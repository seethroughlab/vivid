#pragma once
#include "operator_api/operator.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"
#include "midi_helpers.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * @brief Shared core logic for PatternSeq frame-rate and audio-rate variants.
 *
 * Provides parameter definitions, port layout, and the compute() function.
 * Each cadence variant inherits from this and adds its own process method.
 */
struct PatternSeqCore : vivid::OperatorBase {
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
    vivid::Param<int>   midi_channel {"midi_channel", 1, 1, 16};

    PatternSeqCore() {
        vivid::description(steps, "Number of active steps in the sequence, 1 to 16");
        vivid::description(rate, "Step rate relative to the beat clock");
        vivid::description(gate_length, "Fraction of each step where the gate stays high, 0 to 1");
        vivid::description(probability, "Chance each step fires, 0 = never, 1 = always");
        vivid::description(val_0, "Value output when step 1 is active");
        vivid::description(val_1, "Value output when step 2 is active");
        vivid::description(val_2, "Value output when step 3 is active");
        vivid::description(val_3, "Value output when step 4 is active");
        vivid::description(val_4, "Value output when step 5 is active");
        vivid::description(val_5, "Value output when step 6 is active");
        vivid::description(val_6, "Value output when step 7 is active");
        vivid::description(val_7, "Value output when step 8 is active");
        vivid::description(val_8, "Value output when step 9 is active");
        vivid::description(val_9, "Value output when step 10 is active");
        vivid::description(val_10, "Value output when step 11 is active");
        vivid::description(val_11, "Value output when step 12 is active");
        vivid::description(val_12, "Value output when step 13 is active");
        vivid::description(val_13, "Value output when step 14 is active");
        vivid::description(val_14, "Value output when step 15 is active");
        vivid::description(val_15, "Value output when step 16 is active");
        vivid::description(midi_channel, "MIDI channel for note output, 1 to 16");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&steps);
        out.push_back(&rate);
        out.push_back(&gate_length);
        out.push_back(&probability);
        out.push_back(&val_0);  out.push_back(&val_1);
        out.push_back(&val_2);  out.push_back(&val_3);
        out.push_back(&val_4);  out.push_back(&val_5);
        out.push_back(&val_6);  out.push_back(&val_7);
        out.push_back(&val_8);  out.push_back(&val_9);
        out.push_back(&val_10); out.push_back(&val_11);
        out.push_back(&val_12); out.push_back(&val_13);
        out.push_back(&val_14); out.push_back(&val_15);
        out.push_back(&midi_channel);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_SCALAR,  VIVID_PORT_INPUT});
        out.push_back({"value",      VIVID_PORT_SCALAR,  VIVID_PORT_OUTPUT});
        out.push_back({"trigger",    VIVID_PORT_SCALAR,  VIVID_PORT_OUTPUT});
        out.push_back({"gate",       VIVID_PORT_SCALAR,  VIVID_PORT_OUTPUT});
        out.push_back({"step",       VIVID_PORT_SCALAR,  VIVID_PORT_OUTPUT});
        out.push_back({"pattern",    VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
    }

    void compute(float beat_phase, const float* params, float* output_values,
                 VividLanePort* out_spreads, void** custom_outputs, uint32_t custom_output_count) {
        int n   = std::clamp(static_cast<int>(params[0]), 1, 16);
        int r   = std::clamp(static_cast<int>(params[1]), 0, 8);
        float gl = params[2];
        float prob = params[3];

        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) beat_count_++;
        prev_phase_ = beat_phase;

        float multiplier = kMultipliers[r];
        float total_beats = static_cast<float>(beat_count_) + beat_phase;
        float scaled_phase = total_beats * multiplier;

        int global_step = static_cast<int>(std::floor(scaled_phase));
        int current_step = ((global_step % n) + n) % n;
        float step_phase = scaled_phase - std::floor(scaled_phase);

        float value = params[4 + current_step];

        bool new_step = (current_step != prev_step_);
        prev_step_ = current_step;

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

        output_values[0] = out_value;
        output_values[1] = trigger;
        output_values[2] = gate;
        output_values[3] = static_cast<float>(current_step);

        uint8_t ch = static_cast<uint8_t>(midi_channel.int_value() - 1);
        midi_buf_.count = 0;
        bool gate_high = (gate > 0.5f);
        if (gate_high && !prev_gate_) {
            if (prev_midi_note_ >= 0) {
                vivid_sequencers::midi_note_off(midi_buf_,
                    static_cast<uint8_t>(prev_midi_note_), ch);
            }
            uint8_t note = static_cast<uint8_t>(std::clamp(static_cast<int>(out_value), 0, 127));
            vivid_sequencers::midi_note_on(midi_buf_, note, 100, ch);
            prev_midi_note_ = note;
        } else if (!gate_high && prev_gate_) {
            if (prev_midi_note_ >= 0) {
                vivid_sequencers::midi_note_off(midi_buf_,
                    static_cast<uint8_t>(prev_midi_note_), ch);
                prev_midi_note_ = -1;
            }
        }
        prev_gate_ = gate_high;
        if (custom_outputs && custom_output_count > 0) {
            custom_outputs[0] = &midi_buf_;
        }

        if (out_spreads) {
            auto& sp = out_spreads[4];
            auto len = static_cast<uint32_t>(n);
            if (sp.capacity >= len) {
                sp.length = len;
                for (uint32_t i = 0; i < len; ++i)
                    sp.data[i] = params[4 + i];
            }
        }
    }

protected:
    static constexpr float kMultipliers[] = {
        0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 1.5f, 3.0f, 6.0f
    };

    float prev_phase_ = 0.0f;
    int beat_count_ = 0;
    int prev_step_ = -1;
    bool prev_gate_ = false;
    int prev_midi_note_ = -1;
    VividMidiBuffer midi_buf_ = {};

    static uint32_t xorshift32(uint32_t state) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};
