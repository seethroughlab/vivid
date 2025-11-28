// Noise Operator
// Generates animated fractal noise texture

#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

struct NoiseState : OperatorState {
    float phase = 0.0f;
};

class Noise : public Operator {
public:
    Noise() = default;

    // Fluent API
    Noise& scale(float s) { scale_ = s; return *this; }
    Noise& speed(float s) { speed_ = s; return *this; }
    Noise& octaves(int o) { octaves_ = o; return *this; }
    Noise& lacunarity(float l) { lacunarity_ = l; return *this; }
    Noise& persistence(float p) { persistence_ = p; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        phase_ += ctx.dt() * speed_;

        // Set up shader parameters
        // param0 = scale, param1 = phase, param2 = octaves
        // param3 = lacunarity, param4 = persistence
        Context::ShaderParams params;
        params.param0 = scale_;
        params.param1 = phase_;
        params.param2 = static_cast<float>(octaves_);
        params.param3 = lacunarity_;
        params.param4 = persistence_;

        ctx.runShader("shaders/noise.wgsl", nullptr, output_, params);
        ctx.setOutput("out", output_);
    }

    void cleanup() override {
        // Texture cleanup handled by renderer
    }

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

VIVID_OPERATOR(Noise)
