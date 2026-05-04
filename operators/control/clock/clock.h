#pragma once

#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>
#include <cstring>

/**
 * @brief Master tempo clock generating beat and bar phase signals.
 *
 * Drives time-based operators with a steady pulse. Outputs a 0-1 sawtooth
 * beat phase, bar phase, milliseconds per beat, and a trigger pulse on
 * each beat boundary.
 *
 * @tip Connect beat_phase to any operator with a beat_phase input for tempo sync.
 * @tip Use beat_phase for global tempo sync, not for per-note ADSR triggering.
 * @output beat_phase Sawtooth ramp 0-1 over each beat.
 * @output beat_ms Milliseconds per beat at the current tempo.
 * @output bar_phase Sawtooth ramp 0-1 over each bar.
 * @output beat_trigger Impulse on each beat boundary.
 * @recipe Clock/beat_phase -> LFO/beat_phase
 * @recipe Clock/beat_phase -> ChordProgression/beat_phase
 * @pitfall beat_phase is a global transport signal; it does not create separate per-note envelope state.
 * @family note_source
 * @best_used_with ChordProgression, Envelope, LFO
 * @common_companions Arpeggiator, Sequencer, NotePattern
 * @see LFO, Envelope, Sequencer
 */
struct ClockCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> bpm{"bpm", 120.0f, 1.0f, 300.0f};
    vivid::Param<int>   beats_per_bar{"beats_per_bar", 4, 1, 16};
    vivid::Param<int>   rate_mode{"rate_mode", 0, {"free", "sync"}};
    vivid::Param<int>   sync_division{"sync_division", 2, vivid::metronome_division_labels()};
    vivid::Param<float> phase_offset{"phase_offset", 0.0f, 0.0f, 1.0f};
    double phase_ = 0.0;
    double bar_phase_ = 0.0;
    double prev_phase_ = 0.0;
    double prev_sync_phase_ = 0.0;

    struct MetronomeSample {
        float bpm = 120.0f;
        int beats_per_bar = 4;
        double beats_elapsed = 0.0;
        float beat_phase = 0.0f;
        float bar_phase = 0.0f;
        float beat_ms = 500.0f;
    };

    ClockCore() {
        vivid::semantic_tag(bpm, "bpm");
        vivid::semantic_shape(bpm, "scalar");
        vivid::semantic_unit(bpm, "bpm");
        vivid::description(bpm, "Tempo in beats per minute");
        vivid::description(beats_per_bar, "Number of beats in each bar when the clock is free-running");
        vivid::description(rate_mode, "Clock source: free-running local tempo or synced to the graph metronome");
        vivid::description(sync_division, "Musical note length when syncing to the graph metronome");
        vivid::description(phase_offset, "Phase offset applied after free or synced timing is computed");
    }

    ~ClockCore() override = default;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&bpm);
        out.push_back(&beats_per_bar);
        vivid::display_hint(phase_offset, VIVID_DISPLAY_KNOB);
        out.push_back(&rate_mode);
        out.push_back(&sync_division);
        out.push_back(&phase_offset);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase",    VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "beat_phase",         "scalar", "global_transport_phase",     "Global 0-1 sawtooth phase over one beat."});
        out.push_back({"beat_ms",       VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "time_milliseconds",  "scalar", "tempo_ms_per_beat",          "Milliseconds per beat at the current tempo."});
        out.push_back({"bar_phase",     VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "bar_phase",          "scalar", "global_transport_bar_phase", "Global 0-1 sawtooth phase over one bar."});
        out.push_back({"beat_trigger",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "trigger",            "scalar", "global_transport_trigger",   "Impulse on each beat boundary."});
    }

    static float sync_cycle_beats(int division) {
        return vivid::sync_cycle_beats(division);
    }

    // Advance phase by delta_time seconds and write the 4 output values.
    void advance(double delta_time, const MetronomeSample& metronome, float* out4) {
        const int mode = rate_mode.int_value();
        const double offset = static_cast<double>(phase_offset.value);
        if (mode == 1) {
            const double cycle_beats = static_cast<double>(sync_cycle_beats(sync_division.int_value()));
            const double cycle_phase = std::fmod((metronome.beats_elapsed / cycle_beats) + offset, 1.0);
            const bool wrapped = cycle_phase < prev_sync_phase_;
            prev_sync_phase_ = cycle_phase;
            out4[0] = static_cast<float>(cycle_phase);
            out4[1] = metronome.beat_ms * static_cast<float>(cycle_beats);
            out4[2] = metronome.bar_phase;
            out4[3] = wrapped ? 1.0f : 0.0f;
            return;
        }

        double beats_per_sec = static_cast<double>(bpm.value) / 60.0;
        double bars_per_sec = beats_per_sec / static_cast<double>(beats_per_bar.value);
        prev_phase_ = phase_;
        phase_ += delta_time * beats_per_sec;
        phase_ -= std::floor(phase_);
        bar_phase_ += delta_time * bars_per_sec;
        bar_phase_ -= std::floor(bar_phase_);
        const double phase_with_offset = std::fmod(phase_ + offset, 1.0);
        out4[0] = static_cast<float>(phase_with_offset);
        out4[1] = 60000.0f / bpm.value;
        out4[2] = static_cast<float>(bar_phase_);
        out4[3] = (phase_ < prev_phase_) ? 1.0f : 0.0f;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override;
};
