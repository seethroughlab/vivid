#pragma once

// Vivid Effects 2D - Downsample Operator
// Low-resolution rendering with upscale

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

enum class FilterMode {
    Nearest,   // Pixelated look
    Linear     // Smooth interpolation
};

class Downsample : public TextureOperator {
public:
    Downsample() = default;
    ~Downsample() override;

    // Fluent API
    Downsample& input(TextureOperator* op) { setInput(0, op); return *this; }
    Downsample& resolution(int w, int h) { m_targetW = w; m_targetH = h; return *this; }
    Downsample& filter(FilterMode f) { m_filter = f; return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Downsample"; }

private:
    void createPipeline(Context& ctx);

    int m_targetW = 320;
    int m_targetH = 240;
    FilterMode m_filter = FilterMode::Nearest;

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
