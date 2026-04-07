#pragma once

#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>

/**
 * @brief Euclidean rhythm generator distributing hits evenly across steps.
 *
 * Uses the Bjorklund algorithm to space a given number of hits as evenly
 * as possible across a step count. Rotation shifts the pattern start point.
 *
 * @tip 3 hits in 8 steps gives the classic tresillo rhythm. 5 in 8 gives a cinquillo.
 * @param hits Number of active steps in the pattern.
 * @param steps Total pattern length.
 * @param rotation Rotates the pattern by N steps.
 * @see DrumSequencer, StepSeq, PatternSeq
 */
struct EuclideanCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   hits        {"hits",        3, 0, 32};
    vivid::Param<int>   steps       {"steps",       8, 1, 32};
    vivid::Param<int>   rotation    {"rotation",    0, 0, 31};
    vivid::Param<float> gate_length {"gate_length", 0.5f, 0.01f, 1.0f};
    vivid::Param<int>   rate        {"rate",        2, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T"}};
    vivid::Param<int>   clock_source{"clock_source", vivid::kClockSourceExternal, vivid::clock_source_labels()};

    EuclideanCore() {
        vivid::semantic_tag(hits, "count");
        vivid::semantic_shape(hits, "int");
        vivid::description(hits, "Number of active hits distributed across the pattern");

        vivid::semantic_tag(steps, "count");
        vivid::semantic_shape(steps, "int");
        vivid::description(steps, "Total number of steps in the pattern");

        vivid::semantic_tag(rotation, "index");
        vivid::semantic_shape(rotation, "int");
        vivid::description(rotation, "Rotates the pattern start point by N steps");

        vivid::semantic_tag(gate_length, "phase_01");
        vivid::semantic_shape(gate_length, "scalar");
        vivid::description(gate_length, "Fraction of each step during which the gate stays high");

        vivid::description(rate, "Clock subdivision for step timing");
        vivid::description(clock_source, "Choose whether beat timing comes from the external beat_phase input or the graph metronome");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&hits);        // 0
        out.push_back(&steps);       // 1
        out.push_back(&rotation);    // 2
        out.push_back(&gate_length); // 3
        out.push_back(&rate);        // 4
        out.push_back(&clock_source);// 5
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_SCALAR,     VIVID_PORT_INPUT});   // in[0]
        out.push_back({"trigger",    VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // out[0]
        out.push_back({"gate",       VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // out[1]
        out.push_back({"step",       VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // out[2]
        out.push_back({"pattern",    VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // lane_array[0]
    }

    void compute(float beat_phase, const float* params,
                 VividLaneOutput* output_lanes, float* output_values) {
        int h   = std::clamp(static_cast<int>(params[0]), 0, 32);
        int n   = std::clamp(static_cast<int>(params[1]), 1, 32);
        int rot = std::clamp(static_cast<int>(params[2]), 0, 31);
        float gl = params[3];
        int r   = std::clamp(static_cast<int>(params[4]), 0, 8);

        if (h != prev_hits_ || n != prev_steps_ || rot != prev_rotation_) {
            compute_pattern(h, n, rot);
            prev_hits_ = h;
            prev_steps_ = n;
            prev_rotation_ = rot;
        }

        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) beat_count_++;
        prev_phase_ = beat_phase;

        float multiplier = kMultipliers[r];
        float total_beats = static_cast<float>(beat_count_) + beat_phase;
        float scaled_phase = total_beats * multiplier;

        int global_step = static_cast<int>(std::floor(scaled_phase));
        int current_step = ((global_step % n) + n) % n;
        float step_phase = scaled_phase - std::floor(scaled_phase);

        bool new_step = (current_step != prev_step_);
        prev_step_ = current_step;

        bool is_hit = (pattern_[current_step] != 0);
        output_values[0] = (new_step && is_hit) ? 1.0f : 0.0f;  // trigger
        output_values[1] = (is_hit && step_phase < gl) ? 1.0f : 0.0f;  // gate
        output_values[2] = static_cast<float>(current_step);  // step

        if (output_lanes) {
            auto& pattern_lane = output_lanes[3];
            auto len = static_cast<uint32_t>(n);
            float* buf = pattern_lane.resize(pattern_lane.handle, len);
            if (buf) {
                for (uint32_t i = 0; i < len; ++i)
                    buf[i] = static_cast<float>(pattern_[i]);
                pattern_lane.commit(pattern_lane.handle, len);
            }
        }
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override;

    // Expose pattern for thumbnail access
    const int* current_pattern() const { return pattern_; }

private:
    static constexpr float kMultipliers[] = {
        0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 1.5f, 3.0f, 6.0f
    };

    float prev_phase_ = 0.0f;
    int beat_count_ = 0;
    int prev_step_ = -1;
    int prev_hits_ = -1;
    int prev_steps_ = -1;
    int prev_rotation_ = -1;
    int pattern_[32] = {};

    void compute_pattern(int h, int n, int rot) {
        for (int i = 0; i < 32; ++i) pattern_[i] = 0;
        if (n <= 0) return;
        h = std::clamp(h, 0, n);
        if (h == 0) return;
        if (h == n) {
            for (int i = 0; i < n; ++i) pattern_[i] = 1;
            if (rot > 0) rotate_pattern(n, rot);
            return;
        }

        int seqs[32][32];
        int slen[32];
        for (int i = 0; i < h; ++i)     { seqs[i][0] = 1; slen[i] = 1; }
        for (int i = h; i < n; ++i)      { seqs[i][0] = 0; slen[i] = 1; }

        int left = h;
        int right = n - h;

        while (right > 1) {
            int pairs = std::min(left, right);
            for (int i = 0; i < pairs; ++i) {
                int src = left + i;
                for (int j = 0; j < slen[src]; ++j)
                    seqs[i][slen[i] + j] = seqs[src][j];
                slen[i] += slen[src];
            }

            if (left > right) {
                right = left - pairs;
                left = pairs;
            } else {
                int extra_start = left + pairs;
                int extra_count = right - pairs;
                for (int i = 0; i < extra_count; ++i) {
                    int src = extra_start + i;
                    int dst = pairs + i;
                    for (int j = 0; j < slen[src]; ++j)
                        seqs[dst][j] = seqs[src][j];
                    slen[dst] = slen[src];
                }
                right = right - pairs;
                left = pairs;
            }
        }

        int pos = 0;
        int total = left + right;
        for (int i = 0; i < total && pos < 32; ++i)
            for (int j = 0; j < slen[i] && pos < 32; ++j)
                pattern_[pos++] = seqs[i][j];

        if (rot > 0) rotate_pattern(n, rot);
    }

    void rotate_pattern(int n, int rot) {
        rot = rot % n;
        if (rot == 0) return;
        int tmp[32];
        for (int i = 0; i < n; ++i)
            tmp[i] = pattern_[(i + rot) % n];
        for (int i = 0; i < n; ++i)
            pattern_[i] = tmp[i];
    }
};
