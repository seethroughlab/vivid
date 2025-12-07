#pragma once

// Vivid Effects 2D - Dither Operator
// Ordered dithering with Bayer patterns

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

enum class DitherPattern {
    Bayer2x2,
    Bayer4x4,
    Bayer8x8
};

class Dither : public TextureOperator {
public:
    Dither() = default;
    ~Dither() override;

    // Fluent API
    Dither& input(TextureOperator* op) { setInput(0, op); return *this; }
    Dither& pattern(DitherPattern p) { m_pattern = p; return *this; }
    Dither& levels(int n) { m_levels = n; return *this; }         // Color levels per channel (2-256)
    Dither& strength(float s) { m_strength = s; return *this; }   // Blend with original (0-1)

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Dither"; }

private:
    void createPipeline(Context& ctx);

    DitherPattern m_pattern = DitherPattern::Bayer4x4;
    int m_levels = 8;
    float m_strength = 1.0f;

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
