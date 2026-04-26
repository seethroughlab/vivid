#pragma once
// Internal frame-rate Envelope implementation used by ChildOp<Envelope>
// consumers. The public operator surface uses envelope_fr / envelope_au variants.

#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
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
 * @tip In polyphonic graphs, drive gate from NoteBreakout/voice_gates and lane_ids from NoteBreakout/voice_ids so each lane gets its own ADSR state.
 * @recipe NoteBreakout/voice_gates -> Envelope/gate; NoteBreakout/voice_ids -> Envelope/lane_ids -> VoiceMixer/amp_env_audio
 * @recipe Envelope/value -> Filter/cutoff_mod with NoteBreakout/voice_freqs -> Filter/frequencies
 * @pitfall beat_phase retriggers globally; use gate when you want per-note articulation.
 * @family voice_shaper
 * @best_used_with NoteBreakout, VoiceMixer, Filter
 * @common_companions Clock, ChordProgression, WavetableOsc
 * @param curve Envelope curve shape: linear, exponential, or logarithmic.
 * @see LFO, MSEG, SpreadADSR
 */
struct Envelope : vivid::OperatorBase {
    static constexpr const char* kName   = "Envelope";
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;
    static constexpr uint32_t kMaxVoices = 16;

    vivid::Param<float> attack   {"attack",    0.001f, 0.0f,   0.5f};
    vivid::Param<float> decay    {"decay",     0.2f,   0.01f,  2.0f};
    vivid::Param<float> sustain  {"sustain",   0.7f,   0.0f,   1.0f};
    vivid::Param<float> release  {"release",   0.3f,   0.001f, 10.0f};
    vivid::Param<float> amplitude{"amplitude", 1.0f,   0.0f,   10.0f};
    vivid::Param<float> offset   {"offset",    0.0f,   0.0f,   10.0f};
    vivid::Param<int>   curve    {"curve",     1,      {"linear", "exponential", "logarithmic"}};
    vivid::Param<int>   clock_source{"clock_source", vivid::kClockSourceExternal, vivid::clock_source_labels()};

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
    struct TrackedLane {
        bool occupied = false;
        uint32_t lane_id = 0;
        LaneState fallback_state;
    };
    TrackedLane tracked_lanes_[kMaxVoices];

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
        vivid::description(clock_source, "Choose whether beat retrigger timing comes from the external beat_phase input or the graph metronome");
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
        display_hint(attack,  VIVID_DISPLAY_ADSR);
        display_hint(decay,   VIVID_DISPLAY_ADSR);
        display_hint(sustain, VIVID_DISPLAY_ADSR);
        display_hint(release, VIVID_DISPLAY_ADSR);

        out.push_back(&attack);    // 0
        out.push_back(&decay);     // 1
        out.push_back(&sustain);   // 2
        out.push_back(&release);   // 3
        out.push_back(&amplitude); // 4
        out.push_back(&offset);    // 5
        out.push_back(&curve);     // 6  (normal dropdown, not part of ADSR widget)
        out.push_back(&clock_source); // 7
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        VividPortDescriptor gate_port{"gate", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT};
        vivid::semantic_tag(gate_port, "gate");
        vivid::semantic_shape(gate_port, "lane_array");
        vivid::semantic_intent(gate_port, "per_note_gate");
        vivid::description(gate_port, "Gate input for ADSR triggering. Accepts scalar or lane-array from voices/gates.");
        out.push_back(gate_port);  // 0

        VividPortDescriptor lane_ids_port{"lane_ids", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT};
        vivid::semantic_tag(lane_ids_port, "lane_id");
        vivid::semantic_shape(lane_ids_port, "lane_array");
        vivid::description(lane_ids_port, "Per-voice lane IDs for stable envelope state across reallocation.");
        out.push_back(lane_ids_port);  // 1

        VividPortDescriptor beat_phase_port{"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT};
        vivid::semantic_tag(beat_phase_port, "beat_phase");
        vivid::semantic_shape(beat_phase_port, "scalar");
        vivid::semantic_intent(beat_phase_port, "global_retrigger_phase");
        vivid::description(beat_phase_port, "Global tempo phase. A wrap retriggers the envelope for all lanes.");
        out.push_back(beat_phase_port);  // 2

        VividPortDescriptor value_port{"value", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr,
                                       static_cast<uint8_t>(kMaxVoices)};
        vivid::semantic_tag(value_port, "amplitude_linear");
        vivid::semantic_shape(value_port, "audio_buffer");
        vivid::semantic_intent(value_port, "envelope_output");
        vivid::description(value_port, "Per-voice envelope output. Multi-channel when driven by polyphonic gates.");
        out.push_back(value_port);  // 3
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
                if (t >= 1.0f) {
                    s.env_value = sus;
                    if (!s.gate_ever_on) {
                        s.release_start = sus;
                        s.stage = RELEASE;
                        s.env_progress = 0.0f;
                    } else {
                        s.stage = SUSTAIN;
                    }
                } else {
                    float shaped = shape_decay(t, c);
                    s.env_value = 1.0f - (1.0f - sus) * shaped;
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
                if (t >= 1.0f) {
                    s.env_value = 0.0f;
                    s.stage = IDLE;
                    s.env_progress = 0.0f;
                } else {
                    float shaped = shape_decay(t, c);
                    s.env_value = s.release_start * (1.0f - shaped);
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

    int find_tracked_lane(uint32_t lane_id) const {
        for (uint32_t i = 0; i < kMaxVoices; ++i) {
            if (tracked_lanes_[i].occupied && tracked_lanes_[i].lane_id == lane_id)
                return static_cast<int>(i);
        }
        return -1;
    }

    int ensure_tracked_lane(uint32_t lane_id) {
        int existing = find_tracked_lane(lane_id);
        if (existing >= 0) return existing;
        for (uint32_t i = 0; i < kMaxVoices; ++i) {
            if (!tracked_lanes_[i].occupied) {
                tracked_lanes_[i] = TrackedLane{};
                tracked_lanes_[i].occupied = true;
                tracked_lanes_[i].lane_id = lane_id;
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void clear_tracked_lane(int idx) {
        if (idx < 0 || idx >= static_cast<int>(kMaxVoices)) return;
        tracked_lanes_[idx] = TrackedLane{};
    }

    // ── Frame-rate processing (used by ChildOp<Envelope> and EnvelopeFr) ──

    void process_frame(const VividFrameContext* ctx) {
        float gate_in  = ctx->input_values[0];
        float phase_in = vivid::resolve_clock_phase(
            clock_source.int_value(), ctx->input_values[2], vivid::metronome_transport(ctx));
        float dt = static_cast<float>(ctx->delta_time);

        LaneState& s = ctx->lane_state_fn
            ? *vivid_lane_state(ctx, ctx->lane_id, LaneState)
            : scalar_state_;

        advance_triggers(s, gate_in, phase_in);
        advance_adsr(s, dt, attack.value, decay.value,
                     sustain.value, release.value, curve.int_value());

        ctx->output_values[0] = s.env_value * amplitude.value + offset.value;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override;
};
