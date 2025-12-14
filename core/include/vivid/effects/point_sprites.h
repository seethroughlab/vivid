#pragma once

// Vivid Effects 2D - PointSprites Operator
// Pattern-based point rendering with GPU instancing

#include <vivid/effects/texture_operator.h>
#include <vivid/effects/types.h>
#include <vivid/effects/particle_renderer.h>
#include <glm/glm.hpp>
#include <vector>
#include <random>

namespace vivid::effects {

enum class Pattern {
    Grid,       // Regular grid
    Random,     // Random positions
    Circle,     // Points arranged in a circle
    Spiral,     // Spiral pattern
    Custom      // Positions from external source
};

enum class PointColorMode {
    Solid,      // Single color for all
    Rainbow,    // HSV rainbow based on index
    Gradient,   // Gradient from color1 to color2
    Random      // Random colors
};

class PointSprites : public TextureOperator {
public:
    PointSprites();
    ~PointSprites() override;

    // Fluent API - Pattern
    PointSprites& pattern(Pattern p) { m_pattern = p; m_needsRebuild = true; return *this; }

    // Fluent API - Count
    PointSprites& count(int c) { m_count = c; m_needsRebuild = true; return *this; }

    // Fluent API - Size
    PointSprites& size(float s) { m_size = s; return *this; }
    PointSprites& sizeVariation(float v) { m_sizeVariation = v; m_needsRebuild = true; return *this; }

    // Fluent API - Color
    PointSprites& color(float r, float g, float b, float a = 1.0f) {
        m_color1 = {r, g, b, a};
        return *this;
    }
    PointSprites& color(const glm::vec4& c) { m_color1 = c; return *this; }
    PointSprites& color2(float r, float g, float b, float a = 1.0f) {
        m_color2 = {r, g, b, a};
        return *this;
    }
    PointSprites& color2(const glm::vec4& c) { m_color2 = c; return *this; }
    PointSprites& colorMode(PointColorMode m) { m_colorMode = m; m_needsRebuild = true; return *this; }

    // Fluent API - Animation
    PointSprites& animate(bool a) { m_animate = a; return *this; }
    PointSprites& animateSpeed(float s) { m_animateSpeed = s; return *this; }
    PointSprites& pulseSize(bool p) { m_pulseSize = p; return *this; }
    PointSprites& pulseSpeed(float s) { m_pulseSpeed = s; return *this; }

    // Fluent API - Pattern-specific
    PointSprites& gridCols(int c) { m_gridCols = c; m_needsRebuild = true; return *this; }
    PointSprites& circleRadius(float r) { m_circleRadius = r; m_needsRebuild = true; return *this; }
    PointSprites& spiralTurns(float t) { m_spiralTurns = t; m_needsRebuild = true; return *this; }
    PointSprites& margin(float m) { m_margin = m; m_needsRebuild = true; return *this; }

    // Fluent API - Custom positions (pairs of x,y values)
    PointSprites& positions(const std::vector<float>& pos) {
        m_customPositions = pos;
        m_pattern = Pattern::Custom;
        m_needsRebuild = true;
        return *this;
    }

    // Fluent API - Background
    PointSprites& clearColor(float r, float g, float b, float a = 1.0f) {
        m_clearColor = {r, g, b, a};
        return *this;
    }

    // Fluent API - Random seed
    PointSprites& seed(int s) { m_seed = s; m_needsRebuild = true; return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "PointSprites"; }

private:
    void generatePattern();
    void generateGrid();
    void updateAnimation(float time);
    glm::vec4 getColor(int index, int total, float randomVal, std::mt19937& rng);
    glm::vec4 hsvToRgb(float h, float s, float v);

    // Pattern settings
    Pattern m_pattern = Pattern::Grid;
    int m_count = 100;
    int m_seed = 42;

    // Size settings
    float m_size = 0.02f;
    float m_sizeVariation = 0.0f;

    // Color settings
    PointColorMode m_colorMode = PointColorMode::Solid;
    glm::vec4 m_color1{1.0f, 0.5f, 0.2f, 1.0f};
    glm::vec4 m_color2{0.2f, 0.5f, 1.0f, 1.0f};

    // Animation
    bool m_animate = false;
    float m_animateSpeed = 1.0f;
    float m_phase = 0.0f;
    bool m_pulseSize = false;
    float m_pulseSpeed = 2.0f;

    // Pattern-specific
    int m_gridCols = 0;  // 0 = auto-calculate
    float m_circleRadius = 0.3f;
    float m_spiralTurns = 3.0f;
    float m_margin = 0.05f;

    // Custom positions
    std::vector<float> m_customPositions;

    // Background
    glm::vec4 m_clearColor{0.0f, 0.0f, 0.0f, 1.0f};

    // Internal state
    bool m_needsRebuild = true;
    std::vector<Circle2D> m_circles;
    std::vector<glm::vec2> m_basePositions;

    // Rendering
    ParticleRenderer m_renderer;
    bool m_initialized = false;
};

} // namespace vivid::effects
