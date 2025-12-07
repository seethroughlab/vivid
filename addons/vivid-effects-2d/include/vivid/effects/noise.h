#pragma once

// Vivid Effects 2D - Noise Operator
// Generates animated fractal Perlin noise

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Noise : public TextureOperator {
public:
    Noise() = default;
    ~Noise() override;

    // Fluent API
    Noise& scale(float s) { m_scale = s; return *this; }
    Noise& speed(float s) { m_speed = s; return *this; }
    Noise& octaves(int o) { m_octaves = o; return *this; }
    Noise& lacunarity(float l) { m_lacunarity = l; return *this; }
    Noise& persistence(float p) { m_persistence = p; return *this; }
    Noise& offset(float x, float y) { m_offsetX = x; m_offsetY = y; return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Noise"; }

private:
    void createPipeline(Context& ctx);

    // Parameters
    float m_scale = 4.0f;
    float m_speed = 0.5f;
    int m_octaves = 4;
    float m_lacunarity = 2.0f;
    float m_persistence = 0.5f;
    float m_offsetX = 0.0f;
    float m_offsetY = 0.0f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
