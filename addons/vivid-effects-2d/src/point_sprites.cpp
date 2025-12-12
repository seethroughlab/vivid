// Vivid Effects 2D - PointSprites Operator Implementation
// Pattern-based point rendering with GPU instancing

#include <vivid/effects/point_sprites.h>
#include <vivid/context.h>
#include <cmath>
#include <algorithm>

namespace vivid::effects {

PointSprites::PointSprites() = default;

PointSprites::~PointSprites() {
    cleanup();
}

void PointSprites::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    m_renderer.init(ctx.device(), ctx.queue());

    generatePattern();
    m_initialized = true;
}

void PointSprites::process(Context& ctx) {
    if (!m_initialized) init(ctx);
    checkResize(ctx);

    // PointSprites is animated if animate or pulseSize is enabled
    bool animated = (m_animate || m_pulseSize);
    if (!animated && !needsCook()) return;

    if (m_needsRebuild) {
        generatePattern();
        m_needsRebuild = false;
    }

    // Update animation
    if (m_animate) {
        m_phase += static_cast<float>(ctx.dt()) * m_animateSpeed;
        updateAnimation(static_cast<float>(ctx.time()));
    }

    // Update size pulse
    float sizeMultiplier = 1.0f;
    if (m_pulseSize) {
        sizeMultiplier = 0.5f + 0.5f * std::sin(static_cast<float>(ctx.time()) * m_pulseSpeed);
    }

    // Apply size multiplier if needed
    std::vector<Circle2D> renderCircles;
    if (sizeMultiplier != 1.0f) {
        renderCircles = m_circles;
        for (auto& c : renderCircles) {
            c.radius *= sizeMultiplier;
        }
    } else {
        renderCircles = m_circles;
    }

    // Render
    m_renderer.renderCircles(ctx, renderCircles, m_outputView, m_width, m_height, m_clearColor);

    didCook();
}

void PointSprites::generatePattern() {
    m_circles.clear();
    m_circles.reserve(m_count);

    std::mt19937 rng(m_seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    switch (m_pattern) {
        case Pattern::Grid:
            generateGrid();
            break;

        case Pattern::Random:
            for (int i = 0; i < m_count; i++) {
                float x = m_margin + dist(rng) * (1.0f - 2.0f * m_margin);
                float y = m_margin + dist(rng) * (1.0f - 2.0f * m_margin);
                float s = m_size * (1.0f - m_sizeVariation + dist(rng) * 2.0f * m_sizeVariation);
                glm::vec4 c = getColor(i, m_count, dist(rng), rng);
                m_circles.emplace_back(glm::vec2(x, y), s, c);
            }
            break;

        case Pattern::Circle: {
            for (int i = 0; i < m_count; i++) {
                float angle = static_cast<float>(i) / m_count * 2.0f * 3.14159265f;
                float x = 0.5f + m_circleRadius * std::cos(angle);
                float y = 0.5f + m_circleRadius * std::sin(angle);
                float s = m_size * (1.0f - m_sizeVariation + dist(rng) * 2.0f * m_sizeVariation);
                glm::vec4 c = getColor(i, m_count, dist(rng), rng);
                m_circles.emplace_back(glm::vec2(x, y), s, c);
            }
            break;
        }

        case Pattern::Spiral: {
            for (int i = 0; i < m_count; i++) {
                float t = static_cast<float>(i) / m_count;
                float angle = t * m_spiralTurns * 2.0f * 3.14159265f;
                float radius = m_circleRadius * t;
                float x = 0.5f + radius * std::cos(angle);
                float y = 0.5f + radius * std::sin(angle);
                float s = m_size * (1.0f - m_sizeVariation + dist(rng) * 2.0f * m_sizeVariation);
                glm::vec4 c = getColor(i, m_count, dist(rng), rng);
                m_circles.emplace_back(glm::vec2(x, y), s, c);
            }
            break;
        }

        case Pattern::Custom: {
            // Expect interleaved x,y pairs
            int numPoints = static_cast<int>(m_customPositions.size()) / 2;
            for (int i = 0; i < numPoints; i++) {
                float x = m_customPositions[i * 2];
                float y = m_customPositions[i * 2 + 1];
                float s = m_size * (1.0f - m_sizeVariation + dist(rng) * 2.0f * m_sizeVariation);
                glm::vec4 c = getColor(i, numPoints, dist(rng), rng);
                m_circles.emplace_back(glm::vec2(x, y), s, c);
            }
            break;
        }
    }

    // Store base positions for animation
    m_basePositions.resize(m_circles.size());
    for (size_t i = 0; i < m_circles.size(); i++) {
        m_basePositions[i] = m_circles[i].position;
    }
}

void PointSprites::generateGrid() {
    int cols = m_gridCols;
    if (cols <= 0) {
        cols = static_cast<int>(std::sqrt(static_cast<float>(m_count)));
    }
    int rows = (m_count + cols - 1) / cols;

    float cellW = (1.0f - 2.0f * m_margin) / cols;
    float cellH = (1.0f - 2.0f * m_margin) / rows;

    std::mt19937 rng(m_seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    int idx = 0;
    for (int row = 0; row < rows && idx < m_count; row++) {
        for (int col = 0; col < cols && idx < m_count; col++) {
            float x = m_margin + (col + 0.5f) * cellW;
            float y = m_margin + (row + 0.5f) * cellH;
            float s = m_size * (1.0f - m_sizeVariation + dist(rng) * 2.0f * m_sizeVariation);
            glm::vec4 c = getColor(idx, m_count, dist(rng), rng);
            m_circles.emplace_back(glm::vec2(x, y), s, c);
            idx++;
        }
    }
}

void PointSprites::updateAnimation(float time) {
    for (size_t i = 0; i < m_circles.size(); i++) {
        float offset = static_cast<float>(i) / m_circles.size() * 2.0f * 3.14159265f;
        float dx = 0.01f * std::sin(m_phase + offset);
        float dy = 0.01f * std::cos(m_phase * 0.7f + offset);
        m_circles[i].position = m_basePositions[i] + glm::vec2(dx, dy);
    }
}

glm::vec4 PointSprites::getColor(int index, int total, float randomVal, std::mt19937& rng) {
    switch (m_colorMode) {
        case PointColorMode::Solid:
            return m_color1;

        case PointColorMode::Rainbow: {
            float hue = static_cast<float>(index) / total;
            return hsvToRgb(hue, 0.8f, 1.0f);
        }

        case PointColorMode::Gradient: {
            float t = static_cast<float>(index) / std::max(1, total - 1);
            return glm::mix(m_color1, m_color2, t);
        }

        case PointColorMode::Random: {
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            return glm::vec4(dist(rng), dist(rng), dist(rng), 1.0f);
        }

        default:
            return m_color1;
    }
}

glm::vec4 PointSprites::hsvToRgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;

    glm::vec3 rgb;
    if (h < 1.0f/6.0f)      rgb = {c, x, 0};
    else if (h < 2.0f/6.0f) rgb = {x, c, 0};
    else if (h < 3.0f/6.0f) rgb = {0, c, x};
    else if (h < 4.0f/6.0f) rgb = {0, x, c};
    else if (h < 5.0f/6.0f) rgb = {x, 0, c};
    else                    rgb = {c, 0, x};

    return glm::vec4(rgb + glm::vec3(m), 1.0f);
}

void PointSprites::cleanup() {
    m_renderer.cleanup();
    releaseOutput();
    m_initialized = false;
    m_circles.clear();
    m_basePositions.clear();
}

} // namespace vivid::effects
