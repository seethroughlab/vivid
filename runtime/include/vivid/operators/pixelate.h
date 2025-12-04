#pragma once

#include "vivid/operator.h"

namespace vivid {

/// Pixelation effect - reduces resolution in a blocky way
class Pixelate : public TextureOperator {
public:
    std::string typeName() const override { return "Pixelate"; }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("pixelSize", 8.0f, 1.0f, 128.0f)
        };
    }

    // Fluent API
    Pixelate& pixelSize(float s) { pixelSize_ = s; return *this; }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    float pixelSize_ = 8.0f;

    struct Constants {
        float resolution[2];
        float pixelSize;
        float padding;
    };
};

} // namespace vivid
