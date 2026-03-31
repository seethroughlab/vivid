#pragma once

#include "operator_api/operator.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief Low-frequency oscillator for parameter modulation.
 *
 * Generates periodic or random control signals at frame-rate (~60 Hz) or
 * audio-rate (~48 kHz). Connect the `value` output to any numeric parameter
 * for animation, or wire it into other control operators.
 *
 * Supports seven waveforms including two random modes: **sample & hold**
 * (stepped random on each cycle) and **smooth random** (Catmull-Rom
 * interpolated). Use the `gate` input to reset phase and trigger fade-in;
 * use `beat_phase` with `rate_mode=sync` for tempo-locked modulation.
 *
 * @tip Use unipolar mode when driving parameters that expect 0-1 (like mix knobs).
 * @tip Connect a Clock's beat_phase output and set rate_mode=sync for tempo-locked sweeps.
 * @tip The slew param smooths all waveforms — useful for softening square or S&H steps.
 * @see Envelope, Clock, Math, Smooth
 * @param frequency Oscillation rate in Hz (ignored when synced to beat_phase).
 * @param waveform Shape of the output wave. sample_hold and smooth_random use the seed param.
 * @param rate_mode free = internal clock, sync = driven by beat_phase input.
 * @param polarity bipolar = -1..1, unipolar = 0..1.
 * @param slew Smooths output transitions. Higher values = more lag.
 * @input gate Rising edge resets phase and starts fade-in timer.
 * @input beat_phase External phase source (0-1 sawtooth from Clock) for sync mode.
 * @output value The computed LFO signal.
 */
struct LFO : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName   = "LFO";
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    vivid::Param<float> frequency    {"frequency",     1.0f,  0.01f, 20.0f};
    vivid::Param<float> amplitude    {"amplitude",     1.0f,  0.0f,  10.0f};
    vivid::Param<float> offset       {"offset",        0.0f, -10.0f, 10.0f};
    vivid::Param<int>   waveform     {"waveform",      0, {"sine", "saw", "square", "triangle", "sample_hold", "smooth_random", "noise"}};
    vivid::Param<int>   rate_mode    {"rate_mode",     0, {"free", "sync"}};
    vivid::Param<int>   polarity     {"polarity",      0, {"bipolar", "unipolar"}};
    vivid::Param<float> phase_offset {"phase_offset",  0.0f, 0.0f, 1.0f};
    vivid::Param<float> fade_in      {"fade_in",       0.0f, 0.0f, 5.0f};
    vivid::Param<float> slew         {"slew",          0.0f, 0.0f, 1.0f};
    vivid::Param<int>   distribution {"distribution",  0, {"uniform", "gaussian"}};
    vivid::Param<int>   seed         {"seed",          0, 0, 99999};

    // Per-lane persistent state (used by vivid_lane_state in audio path,
    // and as member state in frame path until Phase C frame lifting).
    struct AudioState {
        double free_phase = 0.0;
        double prev_phase = 0.0;
        float sh_value    = 0.0f;
        float sh_prev     = 0.0f;
        float sh_next     = 0.0f;
        float sh_prev2    = 0.0f;
        uint32_t noise_seed = 12345;
        float elapsed_time = 0.0f;
        bool  gate_seen    = false;
        bool  prev_gate_on = false;
        float slew_value   = 0.0f;
        int   prev_seed    = 0;
    };

    // Member state for frame-rate path (Phase C will migrate this too)
    AudioState frame_state_;

    LFO() {
        vivid::semantic_tag(frequency, "frequency_hz");
        vivid::semantic_shape(frequency, "scalar");
        vivid::semantic_unit(frequency, "Hz");
        vivid::description(frequency, "Oscillation rate in cycles per second");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::description(amplitude, "Peak output level of the waveform");

        vivid::semantic_tag(offset, "amplitude_linear");
        vivid::semantic_shape(offset, "scalar");
        vivid::semantic_intent(offset, "dc_offset");
        vivid::description(offset, "Constant value added to the output signal");

        vivid::description(waveform, "Shape of the oscillation cycle");
        vivid::description(rate_mode, "Free runs independently; sync locks to the global clock");
        vivid::description(polarity, "Bipolar swings above and below zero; unipolar stays positive");
        vivid::description(phase_offset, "Starting point in the cycle (0 = beginning, 1 = full cycle)");
        vivid::description(fade_in, "Seconds to ramp from zero to full amplitude on start");

        vivid::semantic_tag(slew, "probability_01");
        vivid::semantic_shape(slew, "scalar");
        vivid::description(slew, "Smooths abrupt value changes in stepped waveforms");

        vivid::semantic_tag(seed, "seed");
        vivid::semantic_shape(seed, "int");
        vivid::description(seed, "Random seed for noise and sample-hold waveforms");

        vivid::description(distribution, "Noise distribution: uniform or bell-curved gaussian");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(rate_mode,     VIVID_DISPLAY_DEFAULT);
        display_hint(polarity,      VIVID_DISPLAY_DEFAULT);
        display_hint(phase_offset,  VIVID_DISPLAY_KNOB);
        display_hint(fade_in,       VIVID_DISPLAY_KNOB);
        display_hint(slew,          VIVID_DISPLAY_KNOB);

        layout_row(phase_offset, 2, 0);
        layout_row(fade_in,      2, 1);

        out.push_back(&frequency);     // 0
        out.push_back(&phase_offset);  // 1
        out.push_back(&fade_in);       // 2
        out.push_back(&amplitude);     // 3
        out.push_back(&offset);        // 4
        out.push_back(&waveform);      // 5
        out.push_back(&rate_mode);     // 6
        out.push_back(&polarity);      // 7
        out.push_back(&slew);          // 8
        out.push_back(&distribution);  // 9
        out.push_back(&seed);          // 10
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gate",       VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"beat_phase", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"value",      VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    // ── Shared helpers ──────────────────────────────────────────────────

    static float lcg_random_uniform(AudioState& s) {
        s.noise_seed = s.noise_seed * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(s.noise_seed)) / 2147483648.0f;
    }

    static uint32_t lcg_raw(AudioState& s) {
        s.noise_seed = s.noise_seed * 1664525u + 1013904223u;
        return s.noise_seed;
    }

    static float lcg_random_gaussian(AudioState& s) {
        float u1 = (static_cast<float>(lcg_raw(s)) + 1.0f) / 4294967296.0f;
        float u2 = static_cast<float>(lcg_raw(s)) / 4294967295.0f;
        float z = std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
        z /= 3.0f;
        return std::max(-1.0f, std::min(1.0f, z));
    }

    static float lcg_random(AudioState& s, int dist) {
        if (dist == 1)
            return lcg_random_gaussian(s);
        return lcg_random_uniform(s);
    }

    static float catmull_rom(float p0, float p1, float p2, float p3, float t) {
        return 0.5f * ((2.0f * p1) +
               (-p0 + p2) * t +
               (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t +
               (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
    }

    // Advance phase by dt seconds, apply gate/fade tracking.
    // Returns the computed output value for one time step.
    static float compute_one_sample(AudioState& st, float freq, float amp, float off,
                                    int wf, int rm, int pol, int dist, int seed_val,
                                    float ph_off, float fade, float gate_in, float phase_in,
                                    double dt, float slew_amt) {
        // Seed handling: reinit RNG when seed param changes (0 = free-running)
        if (seed_val != st.prev_seed) {
            if (seed_val > 0) st.noise_seed = static_cast<uint32_t>(seed_val);
            st.prev_seed = seed_val;
        }

        // Gate tracking
        bool gate_on = gate_in > 0.5f;
        bool gate_rising = gate_on && !st.prev_gate_on;
        st.prev_gate_on = gate_on;

        if (gate_on && !st.gate_seen) {
            st.elapsed_time = 0.0f;
            st.gate_seen = true;
        } else if (!gate_on) {
            st.gate_seen = false;
        }

        st.elapsed_time += static_cast<float>(dt);

        // Phase computation
        double phase;
        if (rm == 1 && phase_in != 0.0f) {
            phase = std::fmod(static_cast<double>(phase_in) * static_cast<double>(freq), 1.0);
        } else if (phase_in != 0.0f && rm == 0) {
            phase = std::fmod(static_cast<double>(phase_in), 1.0);
        } else {
            st.free_phase += dt * static_cast<double>(freq);
            st.free_phase -= std::floor(st.free_phase);
            phase = st.free_phase;
        }

        phase = std::fmod(phase + static_cast<double>(ph_off), 1.0);

        bool phase_wrapped = (phase < st.prev_phase - 0.5);
        st.prev_phase = phase;

        // Gate rising edge triggers new S&H value (in addition to phase wraps)
        bool new_sample = phase_wrapped || gate_rising;

        double raw = 0.0;
        switch (wf) {
            case 0: raw = std::sin(phase * 2.0 * M_PI); break;
            case 1: raw = 2.0 * phase - 1.0; break;
            case 2: raw = phase < 0.5 ? 1.0 : -1.0; break;
            case 3: raw = 4.0 * (phase < 0.5 ? phase : (1.0 - phase)) - 1.0; break;
            case 4:
                if (new_sample) st.sh_value = lcg_random(st, dist);
                raw = static_cast<double>(st.sh_value);
                break;
            case 5: {
                if (new_sample) {
                    st.sh_prev2 = st.sh_prev;
                    st.sh_prev = st.sh_next;
                    st.sh_next = lcg_random(st, dist);
                    st.sh_value = lcg_random(st, dist);
                }
                float t = static_cast<float>(phase);
                raw = static_cast<double>(catmull_rom(st.sh_prev2, st.sh_prev, st.sh_next, st.sh_value, t));
                break;
            }
            case 6: raw = static_cast<double>(lcg_random(st, dist)); break;
        }

        if (pol == 1) raw = raw * 0.5 + 0.5;

        float output = static_cast<float>(raw) * amp + off;

        // Slew smoothing
        if (slew_amt > 0.001f) {
            float slew_factor = 1.0f - slew_amt * slew_amt * slew_amt;
            st.slew_value += (output - st.slew_value) * slew_factor;
            output = st.slew_value;
        } else {
            st.slew_value = output;
        }

        if (fade > 0.0f && st.elapsed_time < fade) {
            output *= st.elapsed_time / fade;
        }

        return output;
    }

    // ── Frame-rate processing (~60 Hz) ──────────────────────────────────

    void process_frame(const VividFrameContext* ctx) override {
        float gate_in  = ctx->input_values[0];
        float phase_in = ctx->input_values[1];
        double dt = ctx->delta_time;

        int wf   = static_cast<int>(ctx->param_values[5]);   // waveform
        int rm   = static_cast<int>(ctx->param_values[6]);   // rate_mode
        int pol  = static_cast<int>(ctx->param_values[7]);   // polarity
        int dist = static_cast<int>(ctx->param_values[9]);   // distribution
        int sv   = static_cast<int>(ctx->param_values[10]);  // seed
        float freq     = ctx->param_values[0];  // frequency
        float ph_off   = ctx->param_values[1];  // phase_offset
        float fade     = ctx->param_values[2];  // fade_in
        float amp      = ctx->param_values[3];  // amplitude
        float off      = ctx->param_values[4];  // offset
        float slew_amt = ctx->param_values[8];  // slew

        ctx->output_values[0] = compute_one_sample(
            frame_state_, freq, amp, off, wf, rm, pol, dist, sv,
            ph_off, fade, gate_in, phase_in, dt, slew_amt);
    }

    // ── Audio-rate processing (~48 kHz) ─────────────────────────────────

    void process_audio(const VividAudioContext* ctx) override {
        auto& s = *vivid_lane_state(ctx, ctx->lane_id, AudioState);

        float gate_in  = ctx->input_float_values[0];
        float phase_in = ctx->input_float_values[1];
        const double sample_dt = 1.0 / static_cast<double>(ctx->sample_rate);

        int wf   = waveform.int_value();
        int rm   = rate_mode.int_value();
        int pol  = polarity.int_value();
        int dist = distribution.int_value();
        int sv   = seed.int_value();
        float freq     = frequency.value;
        float amp      = amplitude.value;
        float off      = offset.value;
        float ph_off   = static_cast<float>(phase_offset.value);
        float fade     = fade_in.value;
        float slew_amt = slew.value;

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            ctx->output_buffers[0][i] = compute_one_sample(
                s, freq, amp, off, wf, rm, pol, dist, sv,
                ph_off, fade, gate_in, phase_in, sample_dt, slew_amt);
        }
    }
};
