#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief Bloom (glow) post-processing effect.
 *
 * Creates a glow effect around bright areas of the image.
 * Internally performs threshold extraction, blur, and additive blending.
 *
 * Example:
 * @code
 * chain.add<Bloom>("bloom")
 *     .input("scene")
 *     .threshold(0.8f)
 *     .intensity(1.0f)
 *     .radius(10.0f);
 * @endcode
 */
class Bloom : public Operator {
public:
    Bloom() = default;
    explicit Bloom(const std::string& inputNode) : inputNode_(inputNode) {}

    /// Set input texture from another operator
    Bloom& input(const std::string& node) { inputNode_ = node; return *this; }

    /// Set brightness threshold (0-1, default 0.8). Only pixels brighter than this will glow.
    Bloom& threshold(float t) { threshold_ = t; return *this; }

    /// Set bloom intensity (0-2, default 1.0). Higher = stronger glow.
    Bloom& intensity(float i) { intensity_ = i; return *this; }

    /// Set blur radius for the glow (0-50, default 10). Higher = more spread out glow.
    Bloom& radius(float r) { radius_ = r; return *this; }

    /// Set softness of threshold knee (0-1, default 0.5). Higher = smoother falloff.
    Bloom& softness(float s) { softness_ = s; return *this; }

    /// Set number of blur passes (1-5, default 2). More passes = smoother blur.
    Bloom& passes(int p) { passes_ = p; return *this; }

    void init(Context& ctx) override {
        thresholded_ = ctx.createTexture();
        blurTemp_ = ctx.createTexture();
        blurred_ = ctx.createTexture();
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");
        if (!input) return;

        // Step 1: Extract bright areas
        Context::ShaderParams threshParams;
        threshParams.param0 = threshold_;
        threshParams.param1 = softness_;
        ctx.runShader("shaders/bloom_threshold.wgsl", input, thresholded_, threshParams);

        // Step 2: Blur the bright areas (separable Gaussian)
        for (int i = 0; i < passes_; i++) {
            Texture* src = (i == 0) ? &thresholded_ : &blurred_;

            // Horizontal pass
            Context::ShaderParams hParams;
            hParams.param0 = radius_;
            hParams.vec0X = 1.0f;
            hParams.vec0Y = 0.0f;
            ctx.runShader("shaders/blur.wgsl", src, blurTemp_, hParams);

            // Vertical pass
            Context::ShaderParams vParams;
            vParams.param0 = radius_;
            vParams.vec0X = 0.0f;
            vParams.vec0Y = 1.0f;
            ctx.runShader("shaders/blur.wgsl", &blurTemp_, blurred_, vParams);
        }

        // Step 3: Composite bloom with original
        Context::ShaderParams compParams;
        compParams.param0 = intensity_;  // Bloom intensity
        compParams.param1 = 1.0f;        // Full mix (use bloomed result)
        ctx.runShader("shaders/bloom_composite.wgsl", input, &blurred_, output_, compParams);

        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("threshold", threshold_, 0.0f, 1.0f),
            floatParam("intensity", intensity_, 0.0f, 2.0f),
            floatParam("radius", radius_, 0.0f, 50.0f),
            floatParam("softness", softness_, 0.0f, 1.0f),
            intParam("passes", passes_, 1, 5)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    float threshold_ = 0.8f;
    float intensity_ = 1.0f;
    float radius_ = 10.0f;
    float softness_ = 0.5f;
    int passes_ = 2;

    Texture thresholded_;
    Texture blurTemp_;
    Texture blurred_;
    Texture output_;
};

} // namespace vivid
