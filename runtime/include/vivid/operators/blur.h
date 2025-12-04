#pragma once

#include "vivid/operator.h"

namespace vivid {

/// Applies Gaussian blur to an input texture
class Blur : public TextureOperator {
public:
    std::string typeName() const override { return "Blur"; }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("radius", 5.0f, 0.0f, 50.0f),
            intParam("passes", 1, 1, 5)
        };
    }

    // Fluent API
    Blur& radius(float r) { radius_ = r; return *this; }
    Blur& passes(int p) { passes_ = p; return *this; }

    // Chain API - set input by name
    Blur& input(const std::string& name) { inputName_ = name; return *this; }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    float radius_ = 5.0f;
    int passes_ = 1;
    std::string inputName_;

    struct Constants {
        float resolution[2];  // offset 0
        float radius;         // offset 8
        float padding;        // offset 12
    };
};

} // namespace vivid
