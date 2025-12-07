#pragma once

// Vivid Effects 2D - Scanlines Operator
// CRT-style horizontal/vertical lines

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Scanlines : public TextureOperator {
public:
    Scanlines() = default;
    ~Scanlines() override;

    // Fluent API
    Scanlines& input(TextureOperator* op) { setInput(0, op); return *this; }
    Scanlines& spacing(int pixels) { m_spacing = pixels; return *this; }
    Scanlines& thickness(float t) { m_thickness = t; return *this; }    // 0-1
    Scanlines& intensity(float i) { m_intensity = i; return *this; }    // 0-1 darkening amount
    Scanlines& vertical(bool v) { m_vertical = v; return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Scanlines"; }

private:
    void createPipeline(Context& ctx);

    int m_spacing = 2;
    float m_thickness = 0.5f;
    float m_intensity = 0.3f;
    bool m_vertical = false;

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
