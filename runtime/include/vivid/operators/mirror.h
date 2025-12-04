#pragma once

#include "vivid/operator.h"

namespace vivid {

enum class MirrorMode {
    Horizontal = 0,
    Vertical = 1,
    Both = 2,
    Quad = 3,
    Kaleidoscope = 4
};

/// Mirror/Kaleidoscope effect
class Mirror : public TextureOperator {
public:
    std::string typeName() const override { return "Mirror"; }

    std::vector<ParamDecl> params() override {
        return {
            intParam("mode", 0, 0, 4),
            intParam("segments", 6, 2, 32),
            floatParam("angle", 0.0f, 0.0f, 360.0f),
            floatParam("centerX", 0.5f, 0.0f, 1.0f),
            floatParam("centerY", 0.5f, 0.0f, 1.0f)
        };
    }

    // Fluent API
    Mirror& mode(MirrorMode m) { mode_ = m; return *this; }
    Mirror& segments(int s) { segments_ = s; return *this; }
    Mirror& angle(float a) { angle_ = a; return *this; }
    Mirror& center(float x, float y) { centerX_ = x; centerY_ = y; return *this; }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    MirrorMode mode_ = MirrorMode::Horizontal;
    int segments_ = 6;
    float angle_ = 0.0f;
    float centerX_ = 0.5f;
    float centerY_ = 0.5f;

    struct Constants {
        int mode;
        int segments;
        float angle;
        float centerX;
        float centerY;
        float padding0;
        float padding1;
        float padding2;
    };
};

} // namespace vivid
