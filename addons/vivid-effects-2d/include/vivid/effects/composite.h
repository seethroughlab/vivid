#pragma once

// Vivid Effects 2D - Composite Operator
// Blends two textures using various blend modes

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

enum class BlendMode {
    Over,       // Standard alpha blending
    Add,        // Additive
    Multiply,   // Multiply
    Screen,     // Screen
    Overlay,    // Overlay
    Difference  // Difference
};

class Composite : public TextureOperator {
public:
    Composite() = default;
    ~Composite() override;

    // Fluent API
    Composite& mode(BlendMode m) { m_mode = m; return *this; }
    Composite& opacity(float o) { m_opacity = o; return *this; }

    // Input connections (A = base, B = blend layer)
    Composite& inputA(TextureOperator* op) { setInput(0, op); return *this; }
    Composite& inputB(TextureOperator* op) { setInput(1, op); return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Composite"; }

    std::vector<ParamDecl> params() override {
        // Use String type with mode name embedded in the param name
        std::string modeStr = std::string("mode: ") + modeName(m_mode);
        return {
            {modeStr, ParamType::String, 0.0f, 0.0f, {}},
            {"opacity", ParamType::Float, 0.0f, 1.0f, {m_opacity}}
        };
    }

    // Get blend mode name for display
    static const char* modeName(BlendMode m) {
        switch (m) {
            case BlendMode::Over: return "Over";
            case BlendMode::Add: return "Add";
            case BlendMode::Multiply: return "Multiply";
            case BlendMode::Screen: return "Screen";
            case BlendMode::Overlay: return "Overlay";
            case BlendMode::Difference: return "Difference";
            default: return "Unknown";
        }
    }

private:
    void createPipeline(Context& ctx);
    void updateBindGroup(Context& ctx);

    // Parameters
    BlendMode m_mode = BlendMode::Over;
    float m_opacity = 1.0f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Track input textures to detect changes
    WGPUTextureView m_lastInputA = nullptr;
    WGPUTextureView m_lastInputB = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
