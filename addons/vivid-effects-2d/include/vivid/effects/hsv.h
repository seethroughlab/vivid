#pragma once

// Vivid Effects 2D - HSV Operator
// Hue, saturation, and value adjustment

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class HSV : public TextureOperator {
public:
    HSV() = default;
    ~HSV() override;

    // Fluent API
    HSV& input(TextureOperator* op) { setInput(0, op); return *this; }
    HSV& hueShift(float h) { m_hueShift = h; return *this; }      // 0-1 = full rotation
    HSV& saturation(float s) { m_saturation = s; return *this; }  // 0 = grayscale, 1 = normal, >1 = oversaturated
    HSV& value(float v) { m_value = v; return *this; }            // Brightness multiplier

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "HSV"; }

private:
    void createPipeline(Context& ctx);

    float m_hueShift = 0.0f;
    float m_saturation = 1.0f;
    float m_value = 1.0f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
