#pragma once

#include "vivid/operator.h"

namespace vivid {

enum class EdgeDetectMode {
    Sobel = 0,
    Prewitt = 1,
    Laplacian = 2
};

/// Edge detection filter (Sobel, Prewitt, or Laplacian)
class EdgeDetect : public TextureOperator {
public:
    std::string typeName() const override { return "EdgeDetect"; }

    std::vector<ParamDecl> params() override {
        return {
            intParam("mode", 0, 0, 2),
            floatParam("strength", 1.0f, 0.0f, 5.0f),
            floatParam("threshold", 0.0f, 0.0f, 1.0f)
        };
    }

    // Fluent API
    EdgeDetect& mode(EdgeDetectMode m) { mode_ = m; return *this; }
    EdgeDetect& strength(float s) { strength_ = s; return *this; }
    EdgeDetect& threshold(float t) { threshold_ = t; return *this; }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    EdgeDetectMode mode_ = EdgeDetectMode::Sobel;
    float strength_ = 1.0f;
    float threshold_ = 0.0f;

    struct Constants {
        float resolution[2];
        int mode;
        float strength;
        float threshold;
        float padding0;
        float padding1;
        float padding2;
    };
};

} // namespace vivid
