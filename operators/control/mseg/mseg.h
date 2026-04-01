#pragma once

#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>
#include <cstring>
/**
 * @brief Multi-segment envelope generator with per-segment curves and looping.
 *
 * Defines up to 16 breakpoints with configurable time, value, and curve per
 * segment. Supports loop regions for sustained modulation and gate-triggered
 * attack/release behavior.
 *
 * @tip Use loop mode with 2-3 points for a custom-shaped LFO.
 * @see Envelope, LFO, PathAnimate
 */
struct MSEG : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName   = "MSEG";
    static constexpr bool kTimeDependent = true;

    static constexpr int kMaxPoints = 16;
    static constexpr int kMaxCurves = 15; // segments between points

    // --- Visible params ---
    vivid::Param<int>   num_points   {"num_points",   4,    2, 16};
    vivid::Param<float> total_time   {"total_time",   1.0f, 0.01f, 30.0f};
    vivid::Param<bool>  loop_enabled {"loop_enabled", false};
    vivid::Param<int>   loop_start   {"loop_start",   0,    0, 15};
    vivid::Param<int>   loop_end     {"loop_end",     3,    1, 15};
    vivid::Param<float> amplitude    {"amplitude",    1.0f, 0.0f, 10.0f};

    // --- Hidden params: breakpoint data ---
    // pt_time_0..15, pt_value_0..15, pt_curve_0..14
    vivid::Param<float> pt_time[kMaxPoints] = {
        {"pt_time_0",  0.0f, 0.0f, 1.0f}, {"pt_time_1",  0.333f, 0.0f, 1.0f},
        {"pt_time_2",  0.667f, 0.0f, 1.0f}, {"pt_time_3",  1.0f, 0.0f, 1.0f},
        {"pt_time_4",  0.0f, 0.0f, 1.0f}, {"pt_time_5",  0.0f, 0.0f, 1.0f},
        {"pt_time_6",  0.0f, 0.0f, 1.0f}, {"pt_time_7",  0.0f, 0.0f, 1.0f},
        {"pt_time_8",  0.0f, 0.0f, 1.0f}, {"pt_time_9",  0.0f, 0.0f, 1.0f},
        {"pt_time_10", 0.0f, 0.0f, 1.0f}, {"pt_time_11", 0.0f, 0.0f, 1.0f},
        {"pt_time_12", 0.0f, 0.0f, 1.0f}, {"pt_time_13", 0.0f, 0.0f, 1.0f},
        {"pt_time_14", 0.0f, 0.0f, 1.0f}, {"pt_time_15", 0.0f, 0.0f, 1.0f},
    };

    vivid::Param<float> pt_value[kMaxPoints] = {
        {"pt_value_0",  0.0f, 0.0f, 1.0f}, {"pt_value_1",  1.0f, 0.0f, 1.0f},
        {"pt_value_2",  0.5f, 0.0f, 1.0f}, {"pt_value_3",  0.0f, 0.0f, 1.0f},
        {"pt_value_4",  0.0f, 0.0f, 1.0f}, {"pt_value_5",  0.0f, 0.0f, 1.0f},
        {"pt_value_6",  0.0f, 0.0f, 1.0f}, {"pt_value_7",  0.0f, 0.0f, 1.0f},
        {"pt_value_8",  0.0f, 0.0f, 1.0f}, {"pt_value_9",  0.0f, 0.0f, 1.0f},
        {"pt_value_10", 0.0f, 0.0f, 1.0f}, {"pt_value_11", 0.0f, 0.0f, 1.0f},
        {"pt_value_12", 0.0f, 0.0f, 1.0f}, {"pt_value_13", 0.0f, 0.0f, 1.0f},
        {"pt_value_14", 0.0f, 0.0f, 1.0f}, {"pt_value_15", 0.0f, 0.0f, 1.0f},
    };

    vivid::Param<float> pt_curve[kMaxCurves] = {
        {"pt_curve_0",  0.0f, -1.0f, 1.0f}, {"pt_curve_1",  0.0f, -1.0f, 1.0f},
        {"pt_curve_2",  0.0f, -1.0f, 1.0f}, {"pt_curve_3",  0.0f, -1.0f, 1.0f},
        {"pt_curve_4",  0.0f, -1.0f, 1.0f}, {"pt_curve_5",  0.0f, -1.0f, 1.0f},
        {"pt_curve_6",  0.0f, -1.0f, 1.0f}, {"pt_curve_7",  0.0f, -1.0f, 1.0f},
        {"pt_curve_8",  0.0f, -1.0f, 1.0f}, {"pt_curve_9",  0.0f, -1.0f, 1.0f},
        {"pt_curve_10", 0.0f, -1.0f, 1.0f}, {"pt_curve_11", 0.0f, -1.0f, 1.0f},
        {"pt_curve_12", 0.0f, -1.0f, 1.0f}, {"pt_curve_13", 0.0f, -1.0f, 1.0f},
        {"pt_curve_14", 0.0f, -1.0f, 1.0f},
    };

    MSEG() {
        vivid::description(num_points, "Number of breakpoints in the envelope, 2 to 16");

        vivid::semantic_tag(total_time, "time_seconds");
        vivid::semantic_shape(total_time, "scalar");
        vivid::semantic_unit(total_time, "s");
        vivid::description(total_time, "Duration of the full envelope in seconds");

        vivid::description(loop_enabled, "When on, the envelope loops between loop_start and loop_end while the gate is held");
        vivid::description(loop_start, "Breakpoint index where the loop region begins");
        vivid::description(loop_end, "Breakpoint index where the loop region ends");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::semantic_intent(amplitude, "env_amount");
        vivid::description(amplitude, "Scales the entire envelope output");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(total_time, VIVID_DISPLAY_KNOB);
        // total_time: full-width by default

        out.push_back(&num_points);
        out.push_back(&total_time);
        out.push_back(&loop_enabled);
        out.push_back(&loop_start);
        out.push_back(&loop_end);
        out.push_back(&amplitude);

        // Hidden breakpoint params
        for (int i = 0; i < kMaxPoints; ++i) {
            display_hint(pt_time[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(&pt_time[i]);
        }
        for (int i = 0; i < kMaxPoints; ++i) {
            display_hint(pt_value[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(&pt_value[i]);
        }
        for (int i = 0; i < kMaxCurves; ++i) {
            display_hint(pt_curve[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(&pt_curve[i]);
        }
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gate",       VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"value",      VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    // --- Curve interpolation ---
    static float curve_interp(float t, float curve) {
        if (std::abs(curve) < 0.001f) return t; // linear
        float k = curve * 4.0f;
        return (std::exp(k * t) - 1.0f) / (std::exp(k) - 1.0f);
    }

    // Evaluate MSEG at a normalized position [0,1] across active points
    float evaluate(float norm_pos, int np) const {
        if (np < 2) return 0.0f;

        // Find segment
        for (int i = 0; i < np - 1; ++i) {
            float t0 = pt_time[i].value;
            float t1 = pt_time[i + 1].value;
            if (norm_pos <= t1 || i == np - 2) {
                float seg_len = t1 - t0;
                float t = (seg_len > 0.0001f) ? (norm_pos - t0) / seg_len : 0.0f;
                t = std::max(0.0f, std::min(1.0f, t));
                float shaped = curve_interp(t, pt_curve[i].value);
                return pt_value[i].value + (pt_value[i + 1].value - pt_value[i].value) * shaped;
            }
        }
        return pt_value[np - 1].value;
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], static_cast<float>(ctx->delta_time));
        ctx->output_values[0] = current_value_ * amplitude.value;
    }

    void process_audio(const VividAudioContext* ctx) override {
        compute(0.0f, static_cast<float>(ctx->delta_time));
        float val = current_value_ * amplitude.value;
        for (uint32_t i = 0; i < ctx->buffer_size; ++i)
            ctx->output_buffers[0][i] = val;
    }

    void compute(float gate_in, float dt) {
        bool gate_on = gate_in > 0.5f;
        int np = num_points.int_value();
        np = std::max(2, std::min(kMaxPoints, np));

        bool do_loop = loop_enabled.bool_value();
        int ls = std::max(0, std::min(np - 2, loop_start.int_value()));
        int le = std::max(ls + 1, std::min(np - 1, loop_end.int_value()));

        // Gate edge detection
        bool gate_attack  = gate_on && !prev_gate_;
        bool gate_release = !gate_on && prev_gate_;
        prev_gate_ = gate_on;

        if (gate_attack) {
            stage_ = PLAYING;
            elapsed_ = 0.0f;
        } else if (gate_release && (stage_ == PLAYING || stage_ == LOOPING)) {
            release_start_value_ = current_value_;
            stage_ = RELEASING;
            elapsed_ = 0.0f;
        }

        elapsed_ += dt;

        float tt = std::max(0.01f, total_time.value);

        switch (stage_) {
        case PLAYING:
        case LOOPING: {
            float norm_pos = elapsed_ / tt;

            if (do_loop) {
                float loop_start_time = pt_time[ls].value;
                float loop_end_time   = pt_time[le].value;
                float loop_len = loop_end_time - loop_start_time;

                if (norm_pos >= loop_end_time && loop_len > 0.0001f) {
                    // Wrap within loop region
                    float overshoot = norm_pos - loop_start_time;
                    norm_pos = loop_start_time + std::fmod(overshoot, loop_len);
                    stage_ = LOOPING;
                }
            } else {
                if (norm_pos >= 1.0f) {
                    norm_pos = 1.0f;
                    current_value_ = evaluate(norm_pos, np);
                    stage_ = IDLE;
                    break;
                }
            }

            current_value_ = evaluate(norm_pos, np);
            break;
        }

        case RELEASING: {
            // Linear fade from release_start_value_ to 0
            // Use remaining time to end, min 10ms
            float remaining = std::max(0.01f, tt - (elapsed_ + tt * 0.5f));
            remaining = std::max(0.01f, remaining);
            float fade_t = elapsed_ / remaining;
            if (fade_t >= 1.0f) {
                current_value_ = 0.0f;
                stage_ = IDLE;
            } else {
                current_value_ = release_start_value_ * (1.0f - fade_t);
            }
            break;
        }

        case IDLE:
            current_value_ = 0.0f;
            break;
        }
    }

    void draw_inspector(VividInspectorContext* ctx) override;

private:
    enum Stage { IDLE, PLAYING, LOOPING, RELEASING };
    Stage stage_               = IDLE;
    float elapsed_             = 0.0f;
    float release_start_value_ = 0.0f;
    float current_value_       = 0.0f;
    bool  prev_gate_           = false;

    // Inspector interaction state
    int dragged_point_ = -1;
};
