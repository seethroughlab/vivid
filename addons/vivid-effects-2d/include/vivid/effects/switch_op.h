#pragma once

// Vivid Effects 2D - Switch Operator
// Select between multiple inputs by index

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Switch : public TextureOperator {
public:
    Switch() = default;
    ~Switch() override;

    // Fluent API - up to 8 inputs
    Switch& input(int index, TextureOperator* op) { setInput(index, op); return *this; }
    Switch& index(int i) { m_index = i; return *this; }
    Switch& index(float f) { m_index = static_cast<int>(f); return *this; }  // For LFO control
    Switch& blend(float b) { m_blend = b; return *this; }  // Crossfade between adjacent inputs

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Switch"; }

private:
    void createPipeline(Context& ctx);

    int m_index = 0;
    float m_blend = 0.0f;  // 0 = hard switch, >0 = crossfade

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
