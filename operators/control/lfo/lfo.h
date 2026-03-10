#pragma once

#include "operator_api/operator.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct LFO : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "LFO";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> frequency{"frequency", 1.0f, 0.01f, 20.0f};
    vivid::Param<float> amplitude{"amplitude", 1.0f, 0.0f, 10000.0f};
    vivid::Param<float> offset   {"offset",    0.0f, -20000.0f, 20000.0f};
    vivid::Param<int>   waveform {"waveform",  0, {"sine", "saw", "square", "triangle"}};
    double free_phase_ = 0.0;

    LFO() {
        vivid::semantic_tag(frequency, "frequency_hz");
        vivid::semantic_shape(frequency, "scalar");
        vivid::semantic_unit(frequency, "Hz");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");

        vivid::semantic_tag(offset, "amplitude_linear");
        vivid::semantic_shape(offset, "scalar");
        vivid::semantic_intent(offset, "dc_offset");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&frequency);
        out.push_back(&amplitude);
        out.push_back(&offset);
        out.push_back(&waveform);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"phase", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        float phase_in = ctx->input_values[0];
        double phase;
        if (phase_in != 0.0f) {
            // Driven by external source (e.g. Clock beat_phase)
            phase = std::fmod(static_cast<double>(phase_in), 1.0);
        } else {
            // Free-running: accumulate incrementally for smooth frequency changes
            free_phase_ += ctx->delta_time * static_cast<double>(frequency.value);
            free_phase_ -= std::floor(free_phase_);
            phase = free_phase_;
        }

        double raw = 0.0;
        switch (waveform.int_value()) {
            case 0: // sine
                raw = std::sin(phase * 2.0 * M_PI);
                break;
            case 1: // saw (rising from -1 to +1)
                raw = 2.0 * phase - 1.0;
                break;
            case 2: // square
                raw = phase < 0.5 ? 1.0 : -1.0;
                break;
            case 3: // triangle
                raw = 4.0 * (phase < 0.5 ? phase : (1.0 - phase)) - 1.0;
                break;
        }

        float output = static_cast<float>(raw) * amplitude.value + offset.value;
        ctx->output_values[0] = output;
    }
};
