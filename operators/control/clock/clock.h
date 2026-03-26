#pragma once

#include "operator_api/operator.h"
#include <cmath>
#include <cstring>

struct ClockThumbState;

struct Clock : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName   = "Clock";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> bpm{"bpm", 120.0f, 1.0f, 300.0f};
    vivid::Param<int>   beats_per_bar{"beats_per_bar", 4, 1, 16};
    double phase_ = 0.0;
    double bar_phase_ = 0.0;

    Clock() {
        vivid::semantic_tag(bpm, "bpm");
        vivid::semantic_shape(bpm, "scalar");
        vivid::semantic_unit(bpm, "bpm");
    }

    ~Clock() override;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&bpm);
        out.push_back(&beats_per_bar);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"beat_ms",    VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"bar_phase",  VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        double beats_per_sec = static_cast<double>(bpm.value) / 60.0;
        double bars_per_sec = beats_per_sec / static_cast<double>(beats_per_bar.value);
        phase_ += ctx->delta_time * beats_per_sec;
        phase_ -= std::floor(phase_);
        bar_phase_ += ctx->delta_time * bars_per_sec;
        bar_phase_ -= std::floor(bar_phase_);
        ctx->output_values[0] = static_cast<float>(phase_);
        ctx->output_values[1] = 60000.0f / bpm.value;
        ctx->output_values[2] = static_cast<float>(bar_phase_);
    }

    void process_audio(const VividAudioContext* ctx) override {
        double delta_time = static_cast<double>(ctx->buffer_size) / ctx->sample_rate;
        double beats_per_sec = static_cast<double>(bpm.value) / 60.0;
        double bars_per_sec = beats_per_sec / static_cast<double>(beats_per_bar.value);
        phase_ += delta_time * beats_per_sec;
        phase_ -= std::floor(phase_);
        bar_phase_ += delta_time * bars_per_sec;
        bar_phase_ -= std::floor(bar_phase_);
        ctx->output_float_values[0] = static_cast<float>(phase_);
        ctx->output_float_values[1] = 60000.0f / bpm.value;
        ctx->output_float_values[2] = static_cast<float>(bar_phase_);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override;

private:
    ClockThumbState* thumb_state_ = nullptr;

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx);
};
