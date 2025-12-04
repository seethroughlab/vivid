#pragma once

#include "vivid/operator.h"

namespace vivid {

/// Composite blend modes
enum class BlendMode {
    Over = 0,    // Alpha over (Porter-Duff)
    Add = 1,     // Additive
    Multiply = 2,
    Screen = 3,
    Overlay = 4
};

/// Blends two textures using alpha compositing
class Composite : public TextureOperator {
public:
    std::string typeName() const override { return "Composite"; }

    std::vector<ParamDecl> params() override {
        return {
            intParam("mode", 0, 0, 4),
            floatParam("opacity", 1.0f, 0.0f, 1.0f)
        };
    }

    // Fluent API
    Composite& mode(BlendMode m) { mode_ = static_cast<int>(m); return *this; }
    Composite& mode(int m) { mode_ = m; return *this; }
    Composite& opacity(float o) { opacity_ = o; return *this; }

    // Set inputs by name (for chain API)
    Composite& a(const std::string& name) { inputAName_ = name; return *this; }
    Composite& b(const std::string& name) { inputBName_ = name; return *this; }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    int mode_ = 0;
    float opacity_ = 1.0f;
    std::string inputAName_;
    std::string inputBName_;

    struct Constants {
        int mode;
        float opacity;
        float padding[2];
    };
};

} // namespace vivid
