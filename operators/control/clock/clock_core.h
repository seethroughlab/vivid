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
 * @output beat_phase Sawtooth ramp 0-1 over each beat.
 * @output beat_trigger Impulse on each beat boundary.
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
        out.push_back({"beat_phase",   VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"beat_ms",      VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"bar_phase",    VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"beat_trigger", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
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
