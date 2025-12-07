#pragma once

// Vivid Effects 2D - CRT Effect Operator
// Retro CRT monitor simulation

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class CRTEffect : public TextureOperator {
public:
    CRTEffect() = default;
    ~CRTEffect() override;

    // Fluent API
    CRTEffect& input(TextureOperator* op) { setInput(0, op); return *this; }
    CRTEffect& curvature(float c) { m_curvature = c; return *this; }      // 0-1 barrel distortion
    CRTEffect& vignette(float v) { m_vignette = v; return *this; }        // 0-1 edge darkening
    CRTEffect& scanlines(float s) { m_scanlines = s; return *this; }      // 0-1 scanline intensity
    CRTEffect& bloom(float b) { m_bloom = b; return *this; }              // 0-1 phosphor glow
    CRTEffect& chromatic(float c) { m_chromatic = c; return *this; }      // 0-1 RGB separation

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "CRTEffect"; }

private:
    void createPipeline(Context& ctx);

    float m_curvature = 0.1f;
    float m_vignette = 0.3f;
    float m_scanlines = 0.2f;
    float m_bloom = 0.1f;
    float m_chromatic = 0.02f;

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
