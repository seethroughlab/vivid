#pragma once
// Internal frame-rate Envelope implementation used by ChildOp<Envelope>
// consumers. The public operator surface uses envelope_fr / envelope_au variants.

#include "operator_api/operator.h"
#include "operator_api/adsr_inspector.h"
#include <cmath>
#include <cstring>
#include <vector>

/**
 * @brief ADSR envelope generator with curve shaping.
 *
 * Classic attack-decay-sustain-release envelope triggered by a gate input.
 * Supports linear, exponential, and logarithmic curve shapes. Can also
 * retrigger on beat phase wrap for rhythmic envelopes.
 *
 * @input gate Gate signal. Rising edges start the ADSR and falling edges trigger release.
 * @input beat_phase External 0-1 beat ramp. A wrap retriggers the envelope globally.
 * @output value The computed envelope value after amplitude and offset are applied.
 * @tip Connect beat_phase from a Clock to retrigger the envelope rhythmically.
 * @tip In polyphonic graphs, drive gate from voices/gates so each lane gets its own ADSR state.
 * @recipe voices/gates -> EnvelopeAu/gate -> VoiceMixer/amp_env_audio
 * @recipe EnvelopeAu/value -> Filter/cutoff_mod with PolyVoiceAllocator/frequencies -> Filter/frequencies
 * @pitfall beat_phase retriggers globally; use gate when you want per-note articulation.
 * @family voice_shaper
 * @best_used_with PolyVoiceAllocator, VoiceMixer, Filter
 * @common_companions ClockAu, ChordProgressionAu, WavetableOsc
 * @param curve Envelope curve shape: linear, exponential, or logarithmic.
 * @see LFO, MSEG, SpreadADSR
 */
struct Envelope : vivid::OperatorBase {
    static constexpr const char* kName   = "Envelope";
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    vivid::Param<float> attack   {"attack",    0.001f, 0.0f,   0.5f};
    vivid::Param<float> decay    {"decay",     0.2f,   0.01f,  2.0f};
    vivid::Param<float> sustain  {"sustain",   0.7f,   0.0f,   1.0f};
    vivid::Param<float> release  {"release",   0.3f,   0.001f, 10.0f};
    vivid::Param<float> amplitude{"amplitude", 1.0f,   0.0f,   10.0f};
    vivid::Param<float> offset   {"offset",    0.0f,   0.0f,   10.0f};
    vivid::Param<int>   curve    {"curve",     1,      {"linear", "exponential", "logarithmic"}};

    enum Stage : uint8_t { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };

    // Per-lane persistent state for both frame and audio execution.
    // Sourced via vivid_lane_state() when lane-state services are active;
    // scalar_state_ is the fallback for non-lifted scalar execution only.
    struct LaneState {
        Stage stage        = IDLE;
        float env_value    = 0.0f;
        float env_progress = 0.0f;
        float release_start = 0.0f;
        float prev_phase   = 0.0f;
        bool  prev_gate    = false;
        bool  gate_ever_on = false;
    };

    LaneState scalar_state_;

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
        VividPortDescriptor gate_port{"gate", VIVID_PORT_SCALAR, VIVID_PORT_INPUT};
        vivid::semantic_tag(gate_port, "gate");
        vivid::semantic_shape(gate_port, "scalar");
        vivid::semantic_intent(gate_port, "per_note_gate");
        vivid::description(gate_port, "Gate input for ADSR triggering. Use voices/gates for per-note envelopes.");
        out.push_back(gate_port);

        VividPortDescriptor beat_phase_port{"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT};
        vivid::semantic_tag(beat_phase_port, "beat_phase");
        vivid::semantic_shape(beat_phase_port, "scalar");
        vivid::semantic_intent(beat_phase_port, "global_retrigger_phase");
        vivid::description(beat_phase_port, "Global tempo phase. A wrap retriggers the envelope for all lanes.");
        out.push_back(beat_phase_port);

        VividPortDescriptor value_port{"value", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT};
        vivid::semantic_tag(value_port, "amplitude_linear");
        vivid::semantic_shape(value_port, "scalar");
        vivid::semantic_intent(value_port, "envelope_output");
        vivid::description(value_port, "Envelope output signal for amp, filter, or modulation shaping.");
        out.push_back(value_port);
    }

    // ── Shared ADSR step ────────────────────────────────────────────────
    // Advance the ADSR state machine by one time step of `dt` seconds.
    // Gate/phase trigger detection and state transitions are handled here.
    // Returns the final output value (env_value * amp + off).

    static void advance_triggers(LaneState& s, float gate_in, float phase_in) {
        bool gate_on = gate_in > 0.5f;
        if (gate_on) s.gate_ever_on = true;

        bool gate_attack  = gate_on && !s.prev_gate;
        bool gate_release = !gate_on && s.prev_gate;

        float phase_delta = phase_in - s.prev_phase;
        bool phase_wrap = phase_delta < -0.5f;

        s.prev_phase = phase_in;
        s.prev_gate  = gate_on;

        if (gate_attack || phase_wrap) {
            s.stage = ATTACK;
            s.env_progress = 0.0f;
        } else if (gate_release && s.stage != IDLE) {
            s.release_start = s.env_value;
            s.stage = RELEASE;
            s.env_progress = 0.0f;
        }
    }

    static void advance_adsr(LaneState& s, float dt, float atk, float dec, float sus, float rel, int c) {
        s.env_progress += dt;

        switch (s.stage) {
        case ATTACK:
            if (atk > 0.0f) {
                float t_a = s.env_progress / atk;
                if (t_a >= 1.0f) {
                    s.env_value = 1.0f;
                    s.stage = DECAY;
                    s.env_progress = 0.0f;
                } else {
                    s.env_value = shape_attack(t_a, c);
                }
            } else {
                s.env_value = 1.0f;
                s.stage = DECAY;
                s.env_progress = 0.0f;
            }
            break;

        case DECAY:
            if (dec > 0.0f) {
                float t = s.env_progress / dec;
                float shaped = shape_decay(std::min(t, 1.0f), c);
                s.env_value = 1.0f - (1.0f - sus) * shaped;
                if (s.env_value <= sus) {
                    s.env_value = sus;
                    if (!s.gate_ever_on) {
                        s.release_start = sus;
                        s.stage = RELEASE;
                        s.env_progress = 0.0f;
                    } else {
                        s.stage = SUSTAIN;
                    }
                }
            } else {
                s.env_value = sus;
                if (!s.gate_ever_on) {
                    s.release_start = sus;
                    s.stage = RELEASE;
                    s.env_progress = 0.0f;
                } else {
                    s.stage = SUSTAIN;
                }
            }
            break;

        case SUSTAIN:
            s.env_value = sus;
            break;

        case RELEASE:
            if (rel > 0.0f) {
                float t = s.env_progress / rel;
                float shaped = shape_decay(std::min(t, 1.0f), c);
                s.env_value = s.release_start * (1.0f - shaped);
                if (s.env_value <= 0.0f) {
                    s.env_value = 0.0f;
                    s.stage = IDLE;
                }
            } else {
                s.env_value = 0.0f;
                s.stage = IDLE;
            }
            break;

        case IDLE:
            s.env_value = 0.0f;
            break;
        }
    }

    // ── Frame-rate processing (used by ChildOp<Envelope> and EnvelopeFr) ──

    void process_frame(const VividFrameContext* ctx) {
        float gate_in  = ctx->input_values[0];
        float phase_in = ctx->input_values[1];
        float dt = static_cast<float>(ctx->delta_time);

        LaneState& s = ctx->lane_state_fn
            ? *vivid_lane_state(ctx, ctx->lane_id, LaneState)
            : scalar_state_;

        advance_triggers(s, gate_in, phase_in);
        advance_adsr(s, dt, attack.value, decay.value,
                     sustain.value, release.value, curve.int_value());

        ctx->output_values[0] = s.env_value * amplitude.value + offset.value;
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
};
