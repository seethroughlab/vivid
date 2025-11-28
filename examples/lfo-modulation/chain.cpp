// LFO Modulation Example
// Demonstrates using LFO values to modulate visual parameters
//
// This example shows:
// - Generating LFO (Low Frequency Oscillator) values
// - Using oscillator output to drive visual parameters
// - Creating rhythmic, pulsing visuals

#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

struct LFOState : OperatorState {
    float phase1 = 0.0f;
    float phase2 = 0.0f;
};

class LFOModulationExample : public Operator {
public:
    void init(Context& ctx) override {
        noise_ = ctx.createTexture();
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        float dt = ctx.dt();

        // Update LFO phases
        phase1_ += dt * freq1_;
        phase2_ += dt * freq2_;

        // Calculate LFO values (different waveforms)
        // LFO 1: Sine wave for smooth modulation
        float lfo1 = std::sin(phase1_ * 6.28318f) * 0.5f + 0.5f;

        // LFO 2: Triangle wave for linear ramps
        float tri = std::fmod(phase2_, 1.0f);
        float lfo2 = tri < 0.5f ? tri * 2.0f : 2.0f - tri * 2.0f;

        // Map LFO values to visual parameters
        float scale = 2.0f + lfo1 * 8.0f;        // Scale pulses between 2-10
        float speed = 0.2f + lfo2 * 0.8f;        // Speed varies 0.2-1.0
        float brightness = 0.5f + lfo1 * 0.5f;   // Brightness pulses 0.5-1.0

        // Generate modulated noise
        Context::ShaderParams noiseParams;
        noiseParams.param0 = scale;
        noiseParams.param1 = ctx.time() * speed;
        noiseParams.param2 = 4.0f;               // octaves
        noiseParams.param3 = 2.0f;               // lacunarity
        noiseParams.param4 = 0.5f;               // persistence
        ctx.runShader("shaders/noise.wgsl", nullptr, noise_, noiseParams);

        // Apply brightness modulation
        Context::ShaderParams brightParams;
        brightParams.param0 = brightness - 0.5f; // brightness offset
        brightParams.param1 = 1.0f + lfo2 * 0.5f; // contrast
        ctx.runShader("shaders/brightness.wgsl", &noise_, output_, brightParams);

        ctx.setOutput("out", output_);

        // Also output the LFO values for visualization
        ctx.setOutput("lfo1", lfo1);
        ctx.setOutput("lfo2", lfo2);
    }

    std::unique_ptr<OperatorState> saveState() override {
        auto state = std::make_unique<LFOState>();
        state->phase1 = phase1_;
        state->phase2 = phase2_;
        return state;
    }

    void loadState(std::unique_ptr<OperatorState> state) override {
        if (auto* s = dynamic_cast<LFOState*>(state.get())) {
            phase1_ = s->phase1;
            phase2_ = s->phase2;
        }
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("freq1", freq1_, 0.1f, 4.0f),
            floatParam("freq2", freq2_, 0.1f, 4.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    float freq1_ = 0.5f;    // LFO 1 frequency in Hz
    float freq2_ = 0.3f;    // LFO 2 frequency in Hz
    float phase1_ = 0.0f;
    float phase2_ = 0.0f;
    Texture noise_;
    Texture output_;
};

VIVID_OPERATOR(LFOModulationExample)
