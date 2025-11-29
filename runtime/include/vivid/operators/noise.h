#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <cmath>
#include <memory>

namespace vivid {

/// State for preserving Noise animation phase across hot-reloads
struct NoiseState : OperatorState {
    float phase = 0.0f;
};

/**
 * @brief Animated fractal noise generator.
 *
 * Generates animated fractal noise using simplex noise with
 * configurable FBM (Fractal Brownian Motion) parameters.
 *
 * Example:
 * @code
 * Noise noise_;
 * noise_.scale(4.0f).speed(1.0f).octaves(4);
 * @endcode
 */
class Noise : public Operator {
public:
    Noise() = default;

    /// Set noise scale (zoom level)
    Noise& scale(float s) { scale_ = s; return *this; }
    /// Set animation speed
    Noise& speed(float s) { speed_ = s; return *this; }
    /// Set number of FBM octaves (1-8)
    Noise& octaves(int o) { octaves_ = o; return *this; }
    /// Set FBM lacunarity (frequency multiplier per octave)
    Noise& lacunarity(float l) { lacunarity_ = l; return *this; }
    /// Set FBM persistence (amplitude multiplier per octave)
    Noise& persistence(float p) { persistence_ = p; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        phase_ += ctx.dt() * speed_;

        Context::ShaderParams params;
        params.param0 = scale_;
        params.param1 = phase_;
        params.param2 = static_cast<float>(octaves_);
        params.param3 = lacunarity_;
        params.param4 = persistence_;

        ctx.runShader("shaders/noise.wgsl", nullptr, output_, params);
        ctx.setOutput("out", output_);
    }

    void cleanup() override {}

    std::unique_ptr<OperatorState> saveState() override {
        auto state = std::make_unique<NoiseState>();
        state->phase = phase_;
        return state;
    }

    void loadState(std::unique_ptr<OperatorState> state) override {
        if (auto* s = dynamic_cast<NoiseState*>(state.get())) {
            phase_ = s->phase;
        }
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("scale", scale_, 0.1f, 50.0f),
            floatParam("speed", speed_, 0.0f, 10.0f),
            intParam("octaves", octaves_, 1, 8),
            floatParam("lacunarity", lacunarity_, 1.0f, 4.0f),
            floatParam("persistence", persistence_, 0.0f, 1.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    float scale_ = 4.0f;
    float speed_ = 1.0f;
    int octaves_ = 4;
    float lacunarity_ = 2.0f;
    float persistence_ = 0.5f;
    float phase_ = 0.0f;
    Texture output_;
};

} // namespace vivid
