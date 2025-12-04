#pragma once

#include "vivid/operator.h"

namespace Diligent {
    struct ITexture;
    struct ITextureView;
}

namespace vivid {

/// Feedback buffer with decay - creates trailing/persistence effects
class Feedback : public TextureOperator {
public:
    std::string typeName() const override { return "Feedback"; }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("decay", 0.95f, 0.0f, 1.0f),
            floatParam("mix", 0.5f, 0.0f, 1.0f)
        };
    }

    // Fluent API
    Feedback& decay(float d) { decay_ = d; return *this; }
    Feedback& mix(float m) { mix_ = m; return *this; }

    // Process override
    void process(Context& ctx) override;

    // Override cleanup to release feedback buffer
    void cleanup() override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    float decay_ = 0.95f;
    float mix_ = 0.5f;

    // Double-buffered feedback
    Diligent::ITexture* feedbackTex_[2] = {nullptr, nullptr};
    Diligent::ITextureView* feedbackRTV_[2] = {nullptr, nullptr};
    Diligent::ITextureView* feedbackSRV_[2] = {nullptr, nullptr};
    int currentBuffer_ = 0;
    bool initialized_ = false;

    struct Constants {
        float decay;
        float mix;
        float padding[2];
    };

    void createFeedbackBuffers(Context& ctx);
};

} // namespace vivid
