#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <cmath>
#include <vector>

namespace vivid {

/**
 * @brief Low Frequency Oscillator.
 *
 * Outputs a single oscillating value that can drive other parameters.
 * Supports multiple waveforms: sine, saw, square, and triangle.
 *
 * Outputs:
 * - "out": Current oscillator value
 * - "history": Recent value history for visualization
 *
 * Example:
 * @code
 * LFO lfo_;
 * lfo_.freq(0.5f).min(0.0f).max(1.0f).waveform(0); // sine
 * @endcode
 */
class LFO : public Operator {
public:
    LFO() = default;

    /// Set frequency in Hz
    LFO& freq(float f) { freq_ = f; return *this; }
    /// Set minimum output value
    LFO& min(float m) { min_ = m; return *this; }
    /// Set maximum output value
    LFO& max(float m) { max_ = m; return *this; }
    /// Set phase offset (0.0 to 1.0)
    LFO& phase(float p) { phaseOffset_ = p; return *this; }
    /// Set waveform: 0=sine, 1=saw, 2=square, 3=triangle
    LFO& waveform(int w) { waveform_ = w; return *this; }

    void process(Context& ctx) override {
        float t = ctx.time() * freq_ + phaseOffset_;
        float normalized = 0.0f;

        switch (waveform_) {
            case 0:  // Sine
                normalized = (std::sin(t * 2.0f * 3.14159265f) + 1.0f) * 0.5f;
                break;
            case 1:  // Saw (ramp up)
                normalized = std::fmod(t, 1.0f);
                if (normalized < 0.0f) normalized += 1.0f;
                break;
            case 2:  // Square
                normalized = std::fmod(t, 1.0f) < 0.5f ? 0.0f : 1.0f;
                break;
            case 3:  // Triangle
                normalized = std::abs(std::fmod(t * 2.0f, 2.0f) - 1.0f);
                break;
            default:
                normalized = 0.5f;
        }

        value_ = min_ + normalized * (max_ - min_);
        ctx.setOutput("out", value_);

        // Store history for visualization
        history_.push_back(value_);
        if (history_.size() > 64) {
            history_.erase(history_.begin());
        }
        ctx.setOutput("history", history_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("freq", freq_, 0.01f, 100.0f),
            floatParam("min", min_, -1000.0f, 1000.0f),
            floatParam("max", max_, -1000.0f, 1000.0f),
            floatParam("phase", phaseOffset_, 0.0f, 1.0f),
            intParam("waveform", waveform_, 0, 3)
        };
    }

    OutputKind outputKind() override { return OutputKind::Value; }

private:
    float freq_ = 1.0f;
    float min_ = 0.0f;
    float max_ = 1.0f;
    float phaseOffset_ = 0.0f;
    int waveform_ = 0;
    float value_ = 0.0f;
    std::vector<float> history_;
};

} // namespace vivid
