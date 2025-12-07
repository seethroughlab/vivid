#pragma once

// Vivid Effects 2D - Mirror Operator
// Axis mirroring and kaleidoscope effects

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

enum class MirrorMode {
    Horizontal,     // Left-right mirror
    Vertical,       // Top-bottom mirror
    Quad,           // Both axes (4 quadrants)
    Kaleidoscope    // Radial symmetry
};

class Mirror : public TextureOperator {
public:
    Mirror() = default;
    ~Mirror() override;

    // Fluent API
    Mirror& input(TextureOperator* op) { setInput(0, op); return *this; }
    Mirror& mode(MirrorMode m) { m_mode = m; return *this; }
    Mirror& segments(int s) { m_segments = s; return *this; }  // For kaleidoscope
    Mirror& angle(float a) { m_angle = a; return *this; }      // Rotation for kaleidoscope
    Mirror& center(float x, float y) { m_centerX = x; m_centerY = y; return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Mirror"; }

private:
    void createPipeline(Context& ctx);

    MirrorMode m_mode = MirrorMode::Horizontal;
    int m_segments = 6;
    float m_angle = 0.0f;
    float m_centerX = 0.5f;
    float m_centerY = 0.5f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
