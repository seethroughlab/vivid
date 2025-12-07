#pragma once

// Vivid Effects 2D - Shape Operator
// SDF-based shape generator (circle, rect, polygon, star, ring)

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

enum class ShapeType {
    Circle,
    Rectangle,
    RoundedRect,
    Triangle,
    Star,
    Ring,
    Polygon
};

class Shape : public TextureOperator {
public:
    Shape() = default;
    ~Shape() override;

    // Fluent API
    Shape& type(ShapeType t) { m_type = t; return *this; }
    Shape& size(float s) { m_sizeX = s; m_sizeY = s; return *this; }
    Shape& size(float x, float y) { m_sizeX = x; m_sizeY = y; return *this; }
    Shape& position(float x, float y) { m_posX = x; m_posY = y; return *this; }
    Shape& rotation(float r) { m_rotation = r; return *this; }
    Shape& sides(int n) { m_sides = n; return *this; }  // For polygon/star
    Shape& cornerRadius(float r) { m_cornerRadius = r; return *this; }
    Shape& thickness(float t) { m_thickness = t; return *this; }  // For ring/outline
    Shape& softness(float s) { m_softness = s; return *this; }  // Edge softness
    Shape& color(float r, float g, float b, float a = 1.0f) {
        m_colorR = r; m_colorG = g; m_colorB = b; m_colorA = a; return *this;
    }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Shape"; }

private:
    void createPipeline(Context& ctx);

    ShapeType m_type = ShapeType::Circle;
    float m_sizeX = 0.5f;
    float m_sizeY = 0.5f;
    float m_posX = 0.5f;
    float m_posY = 0.5f;
    float m_rotation = 0.0f;
    int m_sides = 5;
    float m_cornerRadius = 0.0f;
    float m_thickness = 0.1f;
    float m_softness = 0.01f;
    float m_colorR = 1.0f;
    float m_colorG = 1.0f;
    float m_colorB = 1.0f;
    float m_colorA = 1.0f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
