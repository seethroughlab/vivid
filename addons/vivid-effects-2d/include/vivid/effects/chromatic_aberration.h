#pragma once

// Vivid Effects 2D - ChromaticAberration Operator
// RGB channel separation effect

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class ChromaticAberration : public TextureOperator {
public:
    ChromaticAberration() = default;
    ~ChromaticAberration() override;

    // Fluent API
    ChromaticAberration& input(TextureOperator* op) { setInput(0, op); return *this; }
    ChromaticAberration& amount(float a) { m_amount = a; return *this; }
    ChromaticAberration& angle(float a) { m_angle = a; return *this; }
    ChromaticAberration& radial(bool r) { m_radial = r; return *this; }  // Radial vs linear

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "ChromaticAberration"; }

private:
    void createPipeline(Context& ctx);

    float m_amount = 0.01f;
    float m_angle = 0.0f;
    bool m_radial = true;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
