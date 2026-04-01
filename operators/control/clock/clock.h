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
struct Clock : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName   = "Clock";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> bpm{"bpm", 120.0f, 1.0f, 300.0f};
    vivid::Param<int>   beats_per_bar{"beats_per_bar", 4, 1, 16};
    double phase_ = 0.0;
    double bar_phase_ = 0.0;
    double prev_phase_ = 0.0;

    Clock() {
        vivid::semantic_tag(bpm, "bpm");
        vivid::semantic_shape(bpm, "scalar");
        vivid::semantic_unit(bpm, "bpm");
        vivid::description(bpm, "Tempo in beats per minute");
        vivid::description(beats_per_bar, "Number of beats in each bar for the bar_trigger output");
    }

    ~Clock() override;

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

    void process_frame(const VividFrameContext* ctx) override {
        double beats_per_sec = static_cast<double>(bpm.value) / 60.0;
        double bars_per_sec = beats_per_sec / static_cast<double>(beats_per_bar.value);
        prev_phase_ = phase_;
        phase_ += ctx->delta_time * beats_per_sec;
        phase_ -= std::floor(phase_);
        bar_phase_ += ctx->delta_time * bars_per_sec;
        bar_phase_ -= std::floor(bar_phase_);
        ctx->output_values[0] = static_cast<float>(phase_);
        ctx->output_values[1] = 60000.0f / bpm.value;
        ctx->output_values[2] = static_cast<float>(bar_phase_);
        ctx->output_values[3] = (phase_ < prev_phase_) ? 1.0f : 0.0f;
    }

    void process_audio(const VividAudioContext* ctx) override {
        double delta_time = static_cast<double>(ctx->buffer_size) / ctx->sample_rate;
        double beats_per_sec = static_cast<double>(bpm.value) / 60.0;
        double bars_per_sec = beats_per_sec / static_cast<double>(beats_per_bar.value);
        prev_phase_ = phase_;
        phase_ += delta_time * beats_per_sec;
        phase_ -= std::floor(phase_);
        bar_phase_ += delta_time * bars_per_sec;
        bar_phase_ -= std::floor(bar_phase_);
        float vals[4];
        vals[0] = static_cast<float>(phase_);
        vals[1] = 60000.0f / bpm.value;
        vals[2] = static_cast<float>(bar_phase_);
        vals[3] = (phase_ < prev_phase_) ? 1.0f : 0.0f;
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 4; ++j)
                ctx->output_buffers[j][i] = vals[j];
        }
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override;

private:
    ClockThumbState* thumb_state_ = nullptr;

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx);
};
