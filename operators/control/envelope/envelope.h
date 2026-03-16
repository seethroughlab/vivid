#pragma once

#include "operator_api/operator.h"
#include "operator_api/adsr_inspector.h"
#include <cmath>
#include <cstring>
#include <vector>

struct EnvelopeThumbState;

struct Envelope : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "Envelope";
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

    ~Envelope() override;

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
        out.push_back({"gate",       VIVID_PORT_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"beat_phase", VIVID_PORT_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"value",      VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
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

    void draw_inspector(VividInspectorContext* ctx) override {
        // Param order: attack=0, decay=1, sustain=2, release=3
        float a = (ctx->param_count > 0) ? ctx->param_values[0] : 0.001f;
        float d = (ctx->param_count > 1) ? ctx->param_values[1] : 0.2f;
        float s = (ctx->param_count > 2) ? ctx->param_values[2] : 0.7f;
        float r = (ctx->param_count > 3) ? ctx->param_values[3] : 0.3f;
        vivid::adsr_inspector::draw(ctx, a, d, s, r, false);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override;

private:
    EnvelopeThumbState* thumb_state_ = nullptr;

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx);
};
