#pragma once

#include "operator_api/operator.h"
#include "operator_api/adsr_inspector.h"
#include <cmath>
#include <cstring>
#include <vector>

struct EnvelopeThumbState;
/**
 * @brief ADSR envelope generator with curve shaping.
 *
 * Classic attack-decay-sustain-release envelope triggered by a gate input.
 * Supports linear, exponential, and logarithmic curve shapes. Can also
 * retrigger on beat phase wrap for rhythmic envelopes.
 *
 * @tip Connect beat_phase from a Clock to retrigger the envelope rhythmically.
 * @param curve Envelope curve shape: linear, exponential, or logarithmic.
 * @see LFO, MSEG, SpreadADSR
 */
struct Envelope : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName   = "Envelope";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> attack   {"attack",    0.001f, 0.0f,   0.5f};
    vivid::Param<float> decay    {"decay",     0.2f,   0.01f,  2.0f};
    vivid::Param<float> sustain  {"sustain",   0.7f,   0.0f,   1.0f};
    vivid::Param<float> release  {"release",   0.3f,   0.001f, 10.0f};
    vivid::Param<float> amplitude{"amplitude", 1.0f,   0.0f,   10.0f};
    vivid::Param<float> offset   {"offset",    0.0f,   0.0f,   10.0f};
    vivid::Param<int>   curve    {"curve",     1,      {"linear", "exponential", "logarithmic"}};

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
        vivid::description(attack, "Time to rise from zero to peak in seconds");

        vivid::semantic_tag(decay, "time_seconds");
        vivid::semantic_shape(decay, "scalar");
        vivid::semantic_unit(decay, "s");
        vivid::description(decay, "Time to fall from peak to sustain level in seconds");

        vivid::semantic_tag(sustain, "amplitude_linear");
        vivid::semantic_shape(sustain, "scalar");
        vivid::description(sustain, "Level held while the gate stays high, 0 to 1");

        vivid::semantic_tag(release, "time_seconds");
        vivid::semantic_shape(release, "scalar");
        vivid::semantic_unit(release, "s");
        vivid::description(release, "Time to fade to zero after the gate drops in seconds");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::semantic_intent(amplitude, "env_amount");
        vivid::description(amplitude, "Scales the entire envelope output");

        vivid::semantic_tag(offset, "amplitude_linear");
        vivid::semantic_shape(offset, "scalar");
        vivid::semantic_intent(offset, "env_offset");
        vivid::description(offset, "Constant value added to the envelope output");

        vivid::description(curve, "Envelope shape: linear, exponential, or logarithmic");
    }

    ~Envelope() override;

    // Curve shaping for attack stage (concave-up for exponential = fast onset)
    static float shape_attack(float t, int curve) {
        t = std::max(0.0f, std::min(1.0f, t));
        switch (curve) {
            case 0: return t;                                // linear
            case 1: return 1.0f - std::exp(-4.0f * t);      // exponential: fast onset
            case 2: return t * t;                            // logarithmic: slow onset
            default: return t;
        }
    }

    // Curve shaping for decay/release stages (fast initial drop for exponential)
    static float shape_decay(float t, int curve) {
        t = std::max(0.0f, std::min(1.0f, t));
        switch (curve) {
            case 0: return t;                                // linear
            case 1: return 1.0f - std::exp(-4.0f * t);      // exponential: fast drop, long tail
            case 2: return t * t;                            // logarithmic: slow initial drop
            default: return t;
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(attack,  VIVID_DISPLAY_KNOB);
        display_hint(decay,   VIVID_DISPLAY_KNOB);
        display_hint(sustain, VIVID_DISPLAY_KNOB);
        display_hint(release, VIVID_DISPLAY_KNOB);

        layout_row(attack,  2, 0);
        layout_row(decay,   2, 1);
        layout_row(sustain, 2, 0);
        layout_row(release, 2, 1);

        out.push_back(&attack);    // 0
        out.push_back(&decay);     // 1
        out.push_back(&sustain);   // 2
        out.push_back(&release);   // 3
        out.push_back(&amplitude); // 4
        out.push_back(&offset);    // 5
        display_hint(curve, VIVID_DISPLAY_DEFAULT);
        out.push_back(&curve);     // 6
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gate",       VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"beat_phase", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"value",      VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    // ── Shared ADSR step ────────────────────────────────────────────────
    // Advance the ADSR state machine by one time step of `dt` seconds.
    // Gate/phase trigger detection and state transitions are handled here.
    // Returns the final output value (env_value * amp + off).

    void advance_triggers(float gate_in, float phase_in) {
        bool gate_on = gate_in > 0.5f;
        if (gate_on) gate_ever_on_ = true;

        bool gate_attack  = gate_on && !prev_gate_;
        bool gate_release = !gate_on && prev_gate_;

        float phase_delta = phase_in - prev_phase_;
        bool phase_wrap = phase_delta < -0.5f;

        prev_phase_ = phase_in;
        prev_gate_  = gate_on;

        if (gate_attack || phase_wrap) {
            stage_ = ATTACK;
            env_progress_ = 0.0f;
        } else if (gate_release && stage_ != IDLE) {
            release_start_ = env_value_;
            stage_ = RELEASE;
            env_progress_ = 0.0f;
        }
    }

    void advance_adsr(float dt, float atk, float dec, float sus, float rel, int c) {
        env_progress_ += dt;

        switch (stage_) {
        case ATTACK:
            if (atk > 0.0f) {
                float t_a = env_progress_ / atk;
                if (t_a >= 1.0f) {
                    env_value_ = 1.0f;
                    stage_ = DECAY;
                    env_progress_ = 0.0f;
                } else {
                    env_value_ = shape_attack(t_a, c);
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
                float shaped = shape_decay(std::min(t, 1.0f), c);
                env_value_ = 1.0f - (1.0f - sus) * shaped;
                if (env_value_ <= sus) {
                    env_value_ = sus;
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
                float shaped = shape_decay(std::min(t, 1.0f), c);
                env_value_ = release_start_ * (1.0f - shaped);
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
    }

    // ── Frame-rate processing (~60 Hz) ──────────────────────────────────

    void process_frame(const VividFrameContext* ctx) override {
        float gate_in  = ctx->input_values[0];
        float phase_in = ctx->input_values[1];
        float dt = static_cast<float>(ctx->delta_time);

        // Param order: attack=0, decay=1, sustain=2, release=3, amplitude=4, offset=5, curve=6
        float atk = ctx->param_values[0];
        float dec = ctx->param_values[1];
        float sus = ctx->param_values[2];
        float rel = ctx->param_values[3];
        float amp = ctx->param_values[4];
        float off = ctx->param_values[5];
        int   c   = static_cast<int>(ctx->param_values[6]);

        advance_triggers(gate_in, phase_in);
        advance_adsr(dt, atk, dec, sus, rel, c);

        ctx->output_values[0] = env_value_ * amp + off;
    }

    // ── Audio-rate processing (~48 kHz) ─────────────────────────────────

    void process_audio(const VividAudioContext* ctx) override {
        float gate_in  = ctx->input_float_values[0];
        float phase_in = ctx->input_float_values[1];

        const float sample_dt = 1.0f / static_cast<float>(ctx->sample_rate);
        float atk = attack.value;
        float dec = decay.value;
        float sus = sustain.value;
        float rel = release.value;
        float amp = amplitude.value;
        float off = offset.value;
        int   c   = curve.int_value();

        // Gate and phase trigger detection (once per buffer, from cross-cadence scalar)
        advance_triggers(gate_in, phase_in);

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            advance_adsr(sample_dt, atk, dec, sus, rel, c);
            ctx->output_buffers[0][i] = env_value_ * amp + off;
        }
    }

    void draw_inspector(VividInspectorContext* ctx) override {
        // Param order: attack=0, decay=1, sustain=2, release=3, amplitude=4, offset=5, curve=6
        float a = (ctx->param_count > 0) ? ctx->param_values[0] : 0.001f;
        float d = (ctx->param_count > 1) ? ctx->param_values[1] : 0.2f;
        float s = (ctx->param_count > 2) ? ctx->param_values[2] : 0.7f;
        float r = (ctx->param_count > 3) ? ctx->param_values[3] : 0.3f;
        int cv = (ctx->param_count > 6) ? static_cast<int>(ctx->param_values[6]) : 1;
        vivid::adsr_inspector::draw(ctx, a, d, s, r, false, cv);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override;

private:
    EnvelopeThumbState* thumb_state_ = nullptr;

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx);
};
