#pragma once

// Vivid Effects 2D - Quantize Operator
// Reduce color palette

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Quantize : public TextureOperator {
public:
    Quantize() = default;
    ~Quantize() override;

    // Fluent API
    Quantize& input(TextureOperator* op) { setInput(0, op); return *this; }
    Quantize& levels(int n) { m_levels = n; return *this; }  // Colors per channel (2-256)

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Quantize"; }

private:
    void createPipeline(Context& ctx);

    int m_levels = 8;

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
