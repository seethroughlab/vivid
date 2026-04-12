#pragma once

#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Shared LFO logic: params, ports, state, and compute_one_sample.
// Included by lfo_fr.cpp and lfo_au.cpp.

struct LFOCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    vivid::Param<float> frequency    {"frequency",     1.0f,  0.01f, 20.0f};
    vivid::Param<float> amplitude    {"amplitude",     1.0f,  0.0f,  10.0f};
    vivid::Param<float> offset       {"offset",        0.0f, -10.0f, 10.0f};
    vivid::Param<int>   waveform     {"waveform",      0, {"sine", "saw", "square", "triangle", "sample_hold", "smooth_random", "noise"}};
    vivid::Param<int>   rate_mode    {"rate_mode",     0, vivid::rate_mode_labels()};
    vivid::Param<int>   sync_division{"sync_division", 2, vivid::metronome_division_labels()};
    vivid::Param<int>   polarity     {"polarity",      0, {"bipolar", "unipolar"}};
    vivid::Param<float> phase_offset {"phase_offset",  0.0f, 0.0f, 1.0f};
    vivid::Param<float> fade_in      {"fade_in",       0.0f, 0.0f, 5.0f};
    vivid::Param<float> slew         {"slew",          0.0f, 0.0f, 1.0f};
    vivid::Param<int>   distribution {"distribution",  0, {"uniform", "gaussian"}};
    vivid::Param<int>   seed         {"seed",          0, 0, 99999};

    // ── Persistent state ────────────────────────────────────────────────
    double free_phase_ = 0.0;
    double prev_phase_ = 0.0;
    float sh_value_    = 0.0f;
    float sh_prev_     = 0.0f;
    float sh_next_     = 0.0f;
    float sh_prev2_    = 0.0f;
    uint32_t noise_seed_ = 12345;
    float elapsed_time_ = 0.0f;
    bool  gate_seen_    = false;
    bool  prev_gate_on_ = false;
    float slew_value_   = 0.0f;
    int   prev_seed_    = 0;

    LFOCore() {
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
        vivid::description(rate_mode, "Free runs internally, follows an external beat_phase input, or locks to the graph metronome");
        vivid::description(sync_division, "Musical note length used when rate_mode is metronome");
        vivid::visible_when_ne(frequency, rate_mode, vivid::kRateModeMetronome);
        vivid::visible_when_eq(sync_division, rate_mode, vivid::kRateModeMetronome);
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
        display_hint(waveform,      VIVID_DISPLAY_LFO);
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
        out.push_back(&sync_division); // 7
        out.push_back(&polarity);      // 8
        out.push_back(&slew);          // 9
        out.push_back(&distribution);  // 10
        out.push_back(&seed);          // 11
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gate",       VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"value",      VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    // ── Shared helpers ──────────────────────────────────────────────────

    float lcg_random_uniform() {
        noise_seed_ = noise_seed_ * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(noise_seed_)) / 2147483648.0f;
    }

    float lcg_random_gaussian() {
        float u1 = (static_cast<float>(lcg_raw()) + 1.0f) / 4294967296.0f;
        float u2 = static_cast<float>(lcg_raw()) / 4294967295.0f;
        float z = std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
        z /= 3.0f;
        return std::max(-1.0f, std::min(1.0f, z));
    }

    uint32_t lcg_raw() {
        noise_seed_ = noise_seed_ * 1664525u + 1013904223u;
        return noise_seed_;
    }

    float lcg_random() {
        if (distribution.int_value() == 1)
            return lcg_random_gaussian();
        return lcg_random_uniform();
    }

    static float catmull_rom(float p0, float p1, float p2, float p3, float t) {
        return 0.5f * ((2.0f * p1) +
               (-p0 + p2) * t +
               (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t +
               (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
    }

    float compute_one_sample(float freq, float amp, float off, int wf, int rm, int sync_div, int pol,
                             float ph_off, float fade, float gate_in, float phase_in,
                             double dt, float slew_amt,
                             const vivid::MetronomeTransport& metronome) {
        int s = seed.int_value();
        if (s != prev_seed_) {
            if (s > 0) noise_seed_ = static_cast<uint32_t>(s);
            prev_seed_ = s;
        }

        bool gate_on = gate_in > 0.5f;
        bool gate_rising = gate_on && !prev_gate_on_;
        prev_gate_on_ = gate_on;

        if (gate_on && !gate_seen_) {
            elapsed_time_ = 0.0f;
            gate_seen_ = true;
        } else if (!gate_on) {
            gate_seen_ = false;
        }

        elapsed_time_ += static_cast<float>(dt);

        double phase;
        if (rm == vivid::kRateModeMetronome) {
            phase = vivid::cycle_phase_from_total_beats(metronome.beats_elapsed, sync_div);
        } else if (rm == vivid::kRateModeExternal) {
            phase = std::fmod(static_cast<double>(phase_in) * static_cast<double>(freq), 1.0);
        } else if (phase_in != 0.0f && rm == vivid::kRateModeFree) {
            phase = std::fmod(static_cast<double>(phase_in), 1.0);
        } else {
            free_phase_ += dt * static_cast<double>(freq);
            free_phase_ -= std::floor(free_phase_);
            phase = free_phase_;
        }

        phase = std::fmod(phase + static_cast<double>(ph_off), 1.0);

        bool phase_wrapped = (phase < prev_phase_ - 0.5);
        prev_phase_ = phase;

        bool new_sample = phase_wrapped || gate_rising;

        double raw = 0.0;
        switch (wf) {
            case 0: raw = std::sin(phase * 2.0 * M_PI); break;
            case 1: raw = 2.0 * phase - 1.0; break;
            case 2: raw = phase < 0.5 ? 1.0 : -1.0; break;
            case 3: raw = 4.0 * (phase < 0.5 ? phase : (1.0 - phase)) - 1.0; break;
            case 4:
                if (new_sample) sh_value_ = lcg_random();
                raw = static_cast<double>(sh_value_);
                break;
            case 5: {
                if (new_sample) {
                    sh_prev2_ = sh_prev_;
                    sh_prev_ = sh_next_;
                    sh_next_ = lcg_random();
                    sh_value_ = lcg_random();
                }
                float t = static_cast<float>(phase);
                raw = static_cast<double>(catmull_rom(sh_prev2_, sh_prev_, sh_next_, sh_value_, t));
                break;
            }
            case 6: raw = static_cast<double>(lcg_random()); break;
        }

        if (pol == 1) raw = raw * 0.5 + 0.5;

        float output = static_cast<float>(raw) * amp + off;

        if (slew_amt > 0.001f) {
            float slew_factor = 1.0f - slew_amt * slew_amt * slew_amt;
            slew_value_ += (output - slew_value_) * slew_factor;
            output = slew_value_;
        } else {
            slew_value_ = output;
        }

        if (fade > 0.0f && elapsed_time_ < fade) {
            output *= elapsed_time_ / fade;
        }

        return output;
    }
};
