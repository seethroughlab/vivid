#pragma once

// Vivid Effects 2D - Pixelate Operator
// Mosaic/pixelation effect

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Pixelate : public TextureOperator {
public:
    Pixelate() = default;
    ~Pixelate() override;

    // Fluent API
    Pixelate& input(TextureOperator* op) { setInput(0, op); return *this; }
    Pixelate& size(float s) { m_sizeX = s; m_sizeY = s; return *this; }
    Pixelate& size(float x, float y) { m_sizeX = x; m_sizeY = y; return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Pixelate"; }

private:
    void createPipeline(Context& ctx);

    float m_sizeX = 10.0f;  // Pixel size in screen pixels
    float m_sizeY = 10.0f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
