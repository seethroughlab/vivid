#pragma once

#include "vivid/operator.h"
#include <glm/glm.hpp>

namespace vivid {

/// Generates a solid color texture
class SolidColor : public TextureOperator {
public:
    std::string typeName() const override { return "SolidColor"; }

    std::vector<ParamDecl> params() override {
        return {
            colorParam("color", 1.0f, 1.0f, 1.0f, 1.0f)
        };
    }

    // Fluent API
    SolidColor& color(float r, float g, float b, float a = 1.0f) {
        color_ = glm::vec4(r, g, b, a);
        return *this;
    }

    SolidColor& color(const glm::vec4& c) {
        color_ = c;
        return *this;
    }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    glm::vec4 color_{1.0f, 1.0f, 1.0f, 1.0f};

    struct Constants {
        float color[4];
    };
};

} // namespace vivid
