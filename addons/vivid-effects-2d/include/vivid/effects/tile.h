#pragma once

// Vivid Effects 2D - Tile Operator
// Texture tiling/repetition with offset

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Tile : public TextureOperator {
public:
    Tile() = default;
    ~Tile() override;

    // Fluent API
    Tile& input(TextureOperator* op) { setInput(0, op); return *this; }
    Tile& repeat(float r) { m_repeatX = r; m_repeatY = r; return *this; }
    Tile& repeat(float x, float y) { m_repeatX = x; m_repeatY = y; return *this; }
    Tile& offset(float x, float y) { m_offsetX = x; m_offsetY = y; return *this; }
    Tile& mirror(bool m) { m_mirror = m; return *this; }  // Mirror at tile boundaries

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Tile"; }

private:
    void createPipeline(Context& ctx);

    float m_repeatX = 2.0f;
    float m_repeatY = 2.0f;
    float m_offsetX = 0.0f;
    float m_offsetY = 0.0f;
    bool m_mirror = false;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
