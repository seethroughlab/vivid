#pragma once

#include "vivid/operator.h"

namespace vivid {

/// Generates animated fractal noise texture
class Noise : public TextureOperator {
public:
    std::string typeName() const override { return "Noise"; }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("scale", 4.0f, 0.1f, 50.0f),
            floatParam("speed", 1.0f, 0.0f, 10.0f),
            intParam("octaves", 4, 1, 8),
            floatParam("lacunarity", 2.0f, 1.0f, 4.0f),
            floatParam("persistence", 0.5f, 0.0f, 1.0f)
        };
    }

    // Fluent API
    Noise& scale(float s) { scale_ = s; return *this; }
    Noise& speed(float s) { speed_ = s; return *this; }
    Noise& octaves(int o) { octaves_ = o; return *this; }
    Noise& lacunarity(float l) { lacunarity_ = l; return *this; }
    Noise& persistence(float p) { persistence_ = p; return *this; }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    float scale_ = 4.0f;
    float speed_ = 1.0f;
    int octaves_ = 4;
    float lacunarity_ = 2.0f;
    float persistence_ = 0.5f;

    struct Constants {
        float scale;
        float time;
        int octaves;
        float lacunarity;
        float persistence;
        float padding[3];
    };
};

} // namespace vivid
