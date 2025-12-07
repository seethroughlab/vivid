#pragma once

// Vivid Effects 2D - Brightness Operator
// Brightness and contrast adjustment

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Brightness : public TextureOperator {
public:
    Brightness() = default;
    ~Brightness() override;

    // Fluent API
    Brightness& input(TextureOperator* op) { setInput(0, op); return *this; }
    Brightness& brightness(float b) { m_brightness = b; return *this; }  // -1 to 1
    Brightness& contrast(float c) { m_contrast = c; return *this; }      // 0 = flat gray, 1 = normal, >1 = high contrast
    Brightness& gamma(float g) { m_gamma = g; return *this; }            // Gamma correction

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Brightness"; }

private:
    void createPipeline(Context& ctx);

    float m_brightness = 0.0f;
    float m_contrast = 1.0f;
    float m_gamma = 1.0f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
