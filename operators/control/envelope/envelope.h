#pragma once

#include "operator_api/operator.h"
#include <cmath>
#include <cstring>
#include <vector>

struct Envelope : vivid::OperatorBase {
    static constexpr const char* kName   = "Envelope";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> attack   {"attack",    0.001f, 0.0f,   0.5f};
    vivid::Param<float> decay    {"decay",     0.2f,   0.01f,  2.0f};
    vivid::Param<float> sustain  {"sustain",   0.7f,   0.0f,   1.0f};
    vivid::Param<float> release  {"release",   0.3f,   0.001f, 10.0f};
    vivid::Param<float> amplitude{"amplitude", 1.0f,   0.0f,   10.0f};
    vivid::Param<float> offset   {"offset",    0.0f,   0.0f,   10.0f};

    enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
    Stage stage_        = IDLE;
    float env_value_    = 0.0f;
    float env_progress_ = 0.0f;
    float release_start_ = 0.0f;
    float prev_phase_   = 0.0f;
    bool  prev_gate_    = false;
    bool  gate_ever_on_ = false;

    Envelope() {
        vivid::semantic_tag(attack, "time_seconds");
        vivid::semantic_shape(attack, "scalar");
        vivid::semantic_unit(attack, "s");

        vivid::semantic_tag(decay, "time_seconds");
        vivid::semantic_shape(decay, "scalar");
        vivid::semantic_unit(decay, "s");

        vivid::semantic_tag(sustain, "amplitude_linear");
        vivid::semantic_shape(sustain, "scalar");

        vivid::semantic_tag(release, "time_seconds");
        vivid::semantic_shape(release, "scalar");
        vivid::semantic_unit(release, "s");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::semantic_intent(amplitude, "env_amount");

        vivid::semantic_tag(offset, "amplitude_linear");
        vivid::semantic_shape(offset, "scalar");
        vivid::semantic_intent(offset, "env_offset");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(attack,  VIVID_DISPLAY_KNOB);
        display_hint(decay,   VIVID_DISPLAY_KNOB);
        display_hint(sustain, VIVID_DISPLAY_KNOB);
        display_hint(release, VIVID_DISPLAY_KNOB);

        layout_row(attack,  4, 0);
        layout_row(decay,   4, 1);
        layout_row(sustain, 4, 2);
        layout_row(release, 4, 3);

        out.push_back(&attack);
        out.push_back(&decay);
        out.push_back(&sustain);
        out.push_back(&release);
        out.push_back(&amplitude);
        out.push_back(&offset);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gate",  VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"phase", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(VividProcessContext* ctx) override {
        float gate_in  = ctx->input_values[0];
        float phase_in = ctx->input_values[1];

        float dt  = static_cast<float>(ctx->delta_time);
        float atk = attack.value;
        float dec = decay.value;
        float sus = sustain.value;
        float rel = release.value;

        bool gate_on = gate_in > 0.5f;
        if (gate_on) gate_ever_on_ = true;

        // Trigger detection
        bool gate_attack  = gate_on && !prev_gate_;
        bool gate_release = !gate_on && prev_gate_;

        float phase_delta = phase_in - prev_phase_;
        bool phase_wrap = phase_delta < -0.5f;

        prev_phase_ = phase_in;
        prev_gate_  = gate_on;

        // Gate-on or phase-wrap -> start attack
        if (gate_attack || phase_wrap) {
            stage_ = ATTACK;
            env_progress_ = 0.0f;
        }
        // Gate-off -> start release (only if gate was ever used)
        else if (gate_release && stage_ != IDLE) {
            release_start_ = env_value_;
            stage_ = RELEASE;
            env_progress_ = 0.0f;
        }

        // Advance state machine
        env_progress_ += dt;

        switch (stage_) {
        case ATTACK:
            if (atk > 0.0f) {
                env_value_ = env_progress_ / atk;
                if (env_value_ >= 1.0f) {
                    env_value_ = 1.0f;
                    stage_ = DECAY;
                    env_progress_ = 0.0f;
                }
            } else {
                env_value_ = 1.0f;
                stage_ = DECAY;
                env_progress_ = 0.0f;
            }
            break;

        case DECAY:
            if (dec > 0.0f) {
                float t = env_progress_ / dec;
                env_value_ = 1.0f - (1.0f - sus) * t;
                if (env_value_ <= sus) {
                    env_value_ = sus;
                    // If gate was never used, skip sustain hold -> release
                    if (!gate_ever_on_) {
                        release_start_ = sus;
                        stage_ = RELEASE;
                        env_progress_ = 0.0f;
                    } else {
                        stage_ = SUSTAIN;
                    }
                }
            } else {
                env_value_ = sus;
                if (!gate_ever_on_) {
                    release_start_ = sus;
                    stage_ = RELEASE;
                    env_progress_ = 0.0f;
                } else {
                    stage_ = SUSTAIN;
                }
            }
            break;

        case SUSTAIN:
            env_value_ = sus;
            break;

        case RELEASE:
            if (rel > 0.0f) {
                float t = env_progress_ / rel;
                env_value_ = release_start_ * (1.0f - t);
                if (env_value_ <= 0.0f) {
                    env_value_ = 0.0f;
                    stage_ = IDLE;
                }
            } else {
                env_value_ = 0.0f;
                stage_ = IDLE;
            }
            break;

        case IDLE:
            env_value_ = 0.0f;
            break;
        }

        ctx->output_values[0] = env_value_ * amplitude.value + offset.value;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        // Param order: attack=0, decay=1, sustain=2, release=3, amplitude=4, offset=5
        float atk = (ctx->param_count > 0) ? ctx->param_values[0] : 0.01f;
        float dec = (ctx->param_count > 1) ? ctx->param_values[1] : 0.2f;
        float sus = (ctx->param_count > 2) ? ctx->param_values[2] : 0.7f;
        float rel = (ctx->param_count > 3) ? ctx->param_values[3] : 0.3f;
        if (atk < 0.0001f) atk = 0.0001f;
        if (dec < 0.001f)  dec = 0.001f;
        if (rel < 0.001f)  rel = 0.001f;
        if (sus < 0.0f) sus = 0.0f;
        if (sus > 1.0f) sus = 1.0f;

        // Sustain display width is 30% of (attack + decay + release)
        float sustain_width = 0.3f * (atk + dec + rel);
        float total_time = atk + dec + sustain_width + rel;

        float w = static_cast<float>(ctx->width);
        float h = static_cast<float>(ctx->height);
        float pad = 4.0f;

        // Colors
        const uint8_t bg_r = 18, bg_g = 20, bg_b = 23, bg_a = 230;
        const uint8_t fill_r = 100, fill_g = 130, fill_b = 170, fill_a = 160;
        const uint8_t line_r = 160, line_g = 190, line_b = 220, line_a = 230;
        const uint8_t level_r = 255, level_g = 220, level_b = 100, level_a = 180;

        // ADSR envelope at time t
        auto env_at = [&](float t) -> float {
            if (t <= atk) {
                return t / atk;                              // Attack: 0->1
            }
            t -= atk;
            if (t <= dec) {
                return 1.0f - (1.0f - sus) * (t / dec);     // Decay: 1->sustain
            }
            t -= dec;
            if (t <= sustain_width) {
                return sus;                                   // Sustain: hold
            }
            t -= sustain_width;
            if (t <= rel) {
                return sus * (1.0f - t / rel);               // Release: sustain->0
            }
            return 0.0f;
        };

        auto x_to_time = [&](float x) -> float {
            return (x - pad) / (w - 2.0f * pad) * total_time;
        };
        auto env_to_y = [&](float e) -> float {
            return pad + (1.0f - e) * (h - 2.0f * pad);
        };

        // Compute curve y for each column
        std::vector<float> curve_y(ctx->width);
        for (uint32_t x = 0; x < ctx->width; ++x) {
            float t = x_to_time(static_cast<float>(x));
            if (t < 0.0f) t = 0.0f;
            if (t > total_time) t = total_time;
            curve_y[x] = env_to_y(env_at(t));
        }

        // Draw pixels
        for (uint32_t y = 0; y < ctx->height; ++y) {
            uint8_t* row = ctx->pixels + y * ctx->stride;
            float fy = static_cast<float>(y);
            for (uint32_t x = 0; x < ctx->width; ++x) {
                uint8_t* px = row + x * 4;
                float cy = curve_y[x];
                float dist_to_curve = fy - cy;

                if (std::fabs(dist_to_curve) < 1.2f) {
                    px[0] = line_r; px[1] = line_g; px[2] = line_b; px[3] = line_a;
                } else if (dist_to_curve > 0.0f) {
                    px[0] = fill_r; px[1] = fill_g; px[2] = fill_b; px[3] = fill_a;
                } else {
                    px[0] = bg_r; px[1] = bg_g; px[2] = bg_b; px[3] = bg_a;
                }
            }
        }

        // Draw current-level horizontal indicator from output value
        if (ctx->output_count > 0) {
            float raw = ctx->output_values[0];
            float amp = (ctx->param_count > 4) ? ctx->param_values[4] : 1.0f;
            float off = (ctx->param_count > 5) ? ctx->param_values[5] : 0.0f;
            float env_val = (amp > 0.0001f) ? (raw - off) / amp : 0.0f;
            if (env_val < 0.0f) env_val = 0.0f;
            if (env_val > 1.0f) env_val = 1.0f;

            float ly = env_to_y(env_val);
            uint32_t iy = static_cast<uint32_t>(ly);
            if (iy < ctx->height) {
                uint8_t* row = ctx->pixels + iy * ctx->stride;
                for (uint32_t x = static_cast<uint32_t>(pad);
                     x < ctx->width - static_cast<uint32_t>(pad); ++x) {
                    uint8_t* px = row + x * 4;
                    px[0] = level_r; px[1] = level_g; px[2] = level_b; px[3] = level_a;
                }
            }
        }
    }
};
