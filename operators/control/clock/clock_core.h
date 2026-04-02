#pragma once

#include "operator_api/operator.h"
#include <cmath>
#include <cstring>

struct ClockThumbState;
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
 * @recipe ClockAu/beat_phase -> LFO/beat_phase
 * @recipe ClockAu/beat_phase -> ChordProgressionAu/beat_phase
 * @pitfall beat_phase is a global transport signal; it does not create separate per-note envelope state.
 * @family note_source
 * @best_used_with ChordProgressionAu, EnvelopeAu, LFO
 * @common_companions Arpeggiator, Sequencer, NotePattern
 * @see LFO, Envelope, Sequencer
 */
struct ClockCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> bpm{"bpm", 120.0f, 1.0f, 300.0f};
    vivid::Param<int>   beats_per_bar{"beats_per_bar", 4, 1, 16};
    double phase_ = 0.0;
    double bar_phase_ = 0.0;
    double prev_phase_ = 0.0;

    ClockCore() {
        vivid::semantic_tag(bpm, "bpm");
        vivid::semantic_shape(bpm, "scalar");
        vivid::semantic_unit(bpm, "bpm");
        vivid::description(bpm, "Tempo in beats per minute");
        vivid::description(beats_per_bar, "Number of beats in each bar for the bar_trigger output");
    }

    ~ClockCore() override;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&bpm);
        out.push_back(&beats_per_bar);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        VividPortDescriptor beat_phase_port{"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT};
        vivid::semantic_tag(beat_phase_port, "beat_phase");
        vivid::semantic_shape(beat_phase_port, "scalar");
        vivid::semantic_intent(beat_phase_port, "global_transport_phase");
        vivid::description(beat_phase_port, "Global 0-1 sawtooth phase over one beat.");
        out.push_back(beat_phase_port);

        VividPortDescriptor beat_ms_port{"beat_ms", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT};
        vivid::semantic_tag(beat_ms_port, "time_milliseconds");
        vivid::semantic_shape(beat_ms_port, "scalar");
        vivid::semantic_intent(beat_ms_port, "tempo_ms_per_beat");
        vivid::description(beat_ms_port, "Milliseconds per beat at the current tempo.");
        out.push_back(beat_ms_port);

        VividPortDescriptor bar_phase_port{"bar_phase", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT};
        vivid::semantic_tag(bar_phase_port, "bar_phase");
        vivid::semantic_shape(bar_phase_port, "scalar");
        vivid::semantic_intent(bar_phase_port, "global_transport_bar_phase");
        vivid::description(bar_phase_port, "Global 0-1 sawtooth phase over one bar.");
        out.push_back(bar_phase_port);

        VividPortDescriptor beat_trigger_port{"beat_trigger", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT};
        vivid::semantic_tag(beat_trigger_port, "trigger");
        vivid::semantic_shape(beat_trigger_port, "scalar");
        vivid::semantic_intent(beat_trigger_port, "global_transport_trigger");
        vivid::description(beat_trigger_port, "Impulse on each beat boundary.");
        out.push_back(beat_trigger_port);
    }

    // Advance phase by delta_time seconds and write the 4 output values.
    void advance(double delta_time, float* out4) {
        double beats_per_sec = static_cast<double>(bpm.value) / 60.0;
        double bars_per_sec = beats_per_sec / static_cast<double>(beats_per_bar.value);
        prev_phase_ = phase_;
        phase_ += delta_time * beats_per_sec;
        phase_ -= std::floor(phase_);
        bar_phase_ += delta_time * bars_per_sec;
        bar_phase_ -= std::floor(bar_phase_);
        out4[0] = static_cast<float>(phase_);
        out4[1] = 60000.0f / bpm.value;
        out4[2] = static_cast<float>(bar_phase_);
        out4[3] = (phase_ < prev_phase_) ? 1.0f : 0.0f;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override;

private:
    ClockThumbState* thumb_state_ = nullptr;

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx);
};
