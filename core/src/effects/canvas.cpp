#include <vivid/effects/canvas.h>
#include <vivid/context.h>
#include "canvas_renderer.h"
#include "font_atlas.h"
#include <mapbox/earcut.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <array>

namespace vivid::effects {

// Constants
static constexpr float PI = 3.14159265358979323846f;
static constexpr float TAU = 2.0f * PI;

// -------------------------------------------------------------------------
// CanvasGradient implementation
// -------------------------------------------------------------------------

void CanvasGradient::addColorStop(float offset, const glm::vec4& color) {
    // Clamp offset to valid range
    offset = std::max(0.0f, std::min(1.0f, offset));

    // Limit number of color stops
    if (colorStops.size() >= MAX_COLOR_STOPS) {
        std::cerr << "[Canvas] Warning: Maximum " << MAX_COLOR_STOPS
                  << " color stops allowed, ignoring additional stops\n";
        return;
    }

    // Insert in sorted order by offset
    ColorStop stop{offset, color};
    auto it = std::lower_bound(colorStops.begin(), colorStops.end(), stop,
        [](const ColorStop& a, const ColorStop& b) { return a.offset < b.offset; });
    colorStops.insert(it, stop);
}

void CanvasGradient::addColorStop(float offset, float r, float g, float b, float a) {
    addColorStop(offset, {r, g, b, a});
}

// -------------------------------------------------------------------------
// Canvas implementation
// -------------------------------------------------------------------------

Canvas::Canvas()
    : m_renderer(std::make_unique<CanvasRenderer>())
    , m_font(std::make_unique<FontAtlas>())
{
}

Canvas::~Canvas() {
    cleanup();
}

// -------------------------------------------------------------------------
// Helper methods
// -------------------------------------------------------------------------

glm::vec2 Canvas::transformPoint(const glm::vec2& p) const {
    glm::vec3 result = m_state.transform * glm::vec3(p, 1.0f);
    return glm::vec2(result.x, result.y);
}

glm::vec4 Canvas::applyAlpha(const glm::vec4& color) const {
    return {color.r, color.g, color.b, color.a * m_state.globalAlpha};
}

// Sample a gradient color at a position (in canvas pixel space, before transform)
static glm::vec4 sampleGradient(const CanvasGradient& gradient, const glm::vec2& pos) {
    if (gradient.colorStops.empty()) {
        return {0, 0, 0, 1};
    }
    if (gradient.colorStops.size() == 1) {
        return gradient.colorStops[0].color;
    }

    float t = 0.0f;

    switch (gradient.type) {
        case GradientType::Linear: {
            // Project position onto gradient line
            glm::vec2 dir = gradient.p1 - gradient.p0;
            float len2 = glm::dot(dir, dir);
            if (len2 > 0.0001f) {
                t = glm::dot(pos - gradient.p0, dir) / len2;
            }
            break;
        }
        case GradientType::Radial: {
            // Interpolate between two circles
            // Simplified: use distance from p0 center, map [r0, r1] to [0, 1]
            float dist = glm::length(pos - gradient.p0);
            float range = gradient.r1 - gradient.r0;
            if (std::abs(range) > 0.0001f) {
                t = (dist - gradient.r0) / range;
            } else {
                t = dist <= gradient.r0 ? 0.0f : 1.0f;
            }
            break;
        }
        case GradientType::Conic: {
            // Angular gradient around center point
            glm::vec2 d = pos - gradient.p0;
            float angle = std::atan2(d.y, d.x) - gradient.startAngle;
            // Normalize to [0, 1]
            t = (angle + PI) / TAU;
            t = t - std::floor(t);  // Wrap to [0, 1]
            break;
        }
    }

    // Clamp t to [0, 1]
    t = std::max(0.0f, std::min(1.0f, t));

    // Find color stops surrounding t
    const auto& stops = gradient.colorStops;
    if (t <= stops.front().offset) {
        return stops.front().color;
    }
    if (t >= stops.back().offset) {
        return stops.back().color;
    }

    // Binary search for the interval
    for (size_t i = 0; i < stops.size() - 1; ++i) {
        if (t >= stops[i].offset && t <= stops[i + 1].offset) {
            float range = stops[i + 1].offset - stops[i].offset;
            float localT = (range > 0.0001f) ? (t - stops[i].offset) / range : 0.0f;
            return glm::mix(stops[i].color, stops[i + 1].color, localT);
        }
    }

    return stops.back().color;
}

// Get fill color at a position (handles both solid color and gradient)
glm::vec4 Canvas::getFillColorAt(const glm::vec2& pos) const {
    if (m_state.fillGradient && !m_state.fillGradient->colorStops.empty()) {
        return applyAlpha(sampleGradient(*m_state.fillGradient, pos));
    }
    return applyAlpha(m_state.fillColor);
}

// Get stroke color at a position (handles both solid color and gradient)
glm::vec4 Canvas::getStrokeColorAt(const glm::vec2& pos) const {
    if (m_state.strokeGradient && !m_state.strokeGradient->colorStops.empty()) {
        return applyAlpha(sampleGradient(*m_state.strokeGradient, pos));
    }
    return applyAlpha(m_state.strokeColor);
}

// -------------------------------------------------------------------------
// Configuration
// -------------------------------------------------------------------------

bool Canvas::loadFont(Context& ctx, const std::string& path, float fontSize) {
    return m_font->load(ctx, path, fontSize);
}

// -------------------------------------------------------------------------
// State Management
// -------------------------------------------------------------------------

void Canvas::fillStyle(const glm::vec4& color) {
    m_state.fillColor = color;
    m_state.fillGradient = nullptr;  // Clear gradient when setting solid color
}

void Canvas::fillStyle(float r, float g, float b, float a) {
    m_state.fillColor = {r, g, b, a};
    m_state.fillGradient = nullptr;
}

void Canvas::strokeStyle(const glm::vec4& color) {
    m_state.strokeColor = color;
    m_state.strokeGradient = nullptr;  // Clear gradient when setting solid color
}

void Canvas::strokeStyle(float r, float g, float b, float a) {
    m_state.strokeColor = {r, g, b, a};
    m_state.strokeGradient = nullptr;
}

void Canvas::fillStyle(const CanvasGradient& gradient) {
    m_state.fillGradient = std::make_shared<CanvasGradient>(gradient);
}

void Canvas::strokeStyle(const CanvasGradient& gradient) {
    m_state.strokeGradient = std::make_shared<CanvasGradient>(gradient);
}

void Canvas::lineWidth(float width) {
    m_state.lineWidth = width;
}

void Canvas::lineCap(LineCap cap) {
    m_state.lineCap = cap;
}

void Canvas::lineJoin(LineJoin join) {
    m_state.lineJoin = join;
}

void Canvas::miterLimit(float limit) {
    m_state.miterLimit = limit;
}

void Canvas::globalAlpha(float alpha) {
    m_state.globalAlpha = alpha;
}

void Canvas::save() {
    m_stateStack.push_back(m_state);
}

void Canvas::restore() {
    if (!m_stateStack.empty()) {
        m_state = m_stateStack.back();
        m_stateStack.pop_back();
        // Sync renderer's clip depth with restored state
        m_renderer->setClipDepth(m_state.clipDepth);
    }
}

// -------------------------------------------------------------------------
// Gradients
// -------------------------------------------------------------------------

CanvasGradient Canvas::createLinearGradient(float x0, float y0, float x1, float y1) {
    CanvasGradient gradient;
    gradient.type = GradientType::Linear;
    gradient.p0 = {x0, y0};
    gradient.p1 = {x1, y1};
    return gradient;
}

CanvasGradient Canvas::createRadialGradient(float x0, float y0, float r0,
                                            float x1, float y1, float r1) {
    CanvasGradient gradient;
    gradient.type = GradientType::Radial;
    gradient.p0 = {x0, y0};
    gradient.r0 = r0;
    gradient.p1 = {x1, y1};
    gradient.r1 = r1;
    return gradient;
}

CanvasGradient Canvas::createConicGradient(float startAngle, float x, float y) {
    CanvasGradient gradient;
    gradient.type = GradientType::Conic;
    gradient.p0 = {x, y};
    gradient.startAngle = startAngle;
    return gradient;
}

// -------------------------------------------------------------------------
// Transforms
// -------------------------------------------------------------------------

void Canvas::translate(float x, float y) {
    glm::mat3 translation(1.0f);
    translation[2][0] = x;
    translation[2][1] = y;
    m_state.transform = m_state.transform * translation;
}

void Canvas::rotate(float radians) {
    float c = std::cos(radians);
    float s = std::sin(radians);
    glm::mat3 rotation(1.0f);
    rotation[0][0] = c;  rotation[1][0] = -s;
    rotation[0][1] = s;  rotation[1][1] = c;
    m_state.transform = m_state.transform * rotation;
}

void Canvas::scale(float x, float y) {
    glm::mat3 scaling(1.0f);
    scaling[0][0] = x;
    scaling[1][1] = y;
    m_state.transform = m_state.transform * scaling;
}

void Canvas::scale(float uniform) {
    scale(uniform, uniform);
}

void Canvas::setTransform(const glm::mat3& matrix) {
    m_state.transform = matrix;
}

void Canvas::resetTransform() {
    m_state.transform = glm::mat3(1.0f);
}

glm::mat3 Canvas::getTransform() const {
    return m_state.transform;
}

// -------------------------------------------------------------------------
// Path API
// -------------------------------------------------------------------------

void Canvas::beginPath() {
    m_currentPath.clear();
    m_pathCursor = {0, 0};
    m_pathStart = {0, 0};
}

void Canvas::closePath() {
    if (!m_currentPath.empty()) {
        m_currentPath.push_back({PathCommandType::ClosePath, {}});
        m_pathCursor = m_pathStart;
    }
}

void Canvas::moveTo(float x, float y) {
    m_currentPath.push_back({PathCommandType::MoveTo, {x, y}});
    m_pathCursor = {x, y};
    m_pathStart = m_pathCursor;
}

void Canvas::lineTo(float x, float y) {
    m_currentPath.push_back({PathCommandType::LineTo, {x, y}});
    m_pathCursor = {x, y};
}

void Canvas::arc(float x, float y, float radius, float startAngle, float endAngle, bool counterclockwise) {
    m_currentPath.push_back({PathCommandType::Arc, {x, y, radius, startAngle, endAngle, counterclockwise ? 1.0f : 0.0f}});
    // Update cursor to end of arc
    m_pathCursor = {x + radius * std::cos(endAngle), y + radius * std::sin(endAngle)};
}

void Canvas::arcTo(float x1, float y1, float x2, float y2, float radius) {
    m_currentPath.push_back({PathCommandType::ArcTo, {x1, y1, x2, y2, radius}});
    // arcTo ends at the tangent point, which is complex to calculate here
    // We'll compute it during tessellation
}

void Canvas::quadraticCurveTo(float cpx, float cpy, float x, float y) {
    m_currentPath.push_back({PathCommandType::QuadraticCurveTo, {cpx, cpy, x, y}});
    m_pathCursor = {x, y};
}

void Canvas::bezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) {
    m_currentPath.push_back({PathCommandType::BezierCurveTo, {cp1x, cp1y, cp2x, cp2y, x, y}});
    m_pathCursor = {x, y};
}

void Canvas::pathRect(float x, float y, float w, float h) {
    moveTo(x, y);
    lineTo(x + w, y);
    lineTo(x + w, y + h);
    lineTo(x, y + h);
    closePath();
}

// Tessellation helpers

void Canvas::tessellateArc(std::vector<glm::vec2>& points, float cx, float cy, float radius,
                           float startAngle, float endAngle, bool ccw) const {
    // Determine sweep angle
    float sweep = endAngle - startAngle;
    if (ccw) {
        if (sweep > 0) sweep -= TAU;
    } else {
        if (sweep < 0) sweep += TAU;
    }

    // Calculate number of segments based on arc length and radius
    int segments = std::max(8, static_cast<int>(std::abs(sweep * radius) / 4.0f));

    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / segments;
        float angle = startAngle + sweep * t;
        float px = cx + radius * std::cos(angle);
        float py = cy + radius * std::sin(angle);
        points.push_back(transformPoint({px, py}));
    }
}

void Canvas::tessellateQuadratic(std::vector<glm::vec2>& points, const glm::vec2& start,
                                  float cpx, float cpy, float x, float y) const {
    const int segments = 16;
    for (int i = 1; i <= segments; ++i) {
        float t = static_cast<float>(i) / segments;
        float t2 = t * t;
        float mt = 1.0f - t;
        float mt2 = mt * mt;

        float px = mt2 * start.x + 2.0f * mt * t * cpx + t2 * x;
        float py = mt2 * start.y + 2.0f * mt * t * cpy + t2 * y;
        points.push_back(transformPoint({px, py}));
    }
}

void Canvas::tessellateBezier(std::vector<glm::vec2>& points, const glm::vec2& start,
                               float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) const {
    const int segments = 20;
    for (int i = 1; i <= segments; ++i) {
        float t = static_cast<float>(i) / segments;
        float t2 = t * t;
        float t3 = t2 * t;
        float mt = 1.0f - t;
        float mt2 = mt * mt;
        float mt3 = mt2 * mt;

        float px = mt3 * start.x + 3.0f * mt2 * t * cp1x + 3.0f * mt * t2 * cp2x + t3 * x;
        float py = mt3 * start.y + 3.0f * mt2 * t * cp1y + 3.0f * mt * t2 * cp2y + t3 * y;
        points.push_back(transformPoint({px, py}));
    }
}

std::vector<glm::vec2> Canvas::pathToPolygon() const {
    std::vector<glm::vec2> points;
    glm::vec2 cursor = {0, 0};
    glm::vec2 subpathStart = {0, 0};

    for (const auto& cmd : m_currentPath) {
        switch (cmd.type) {
            case PathCommandType::MoveTo:
                cursor = {cmd.params[0], cmd.params[1]};
                subpathStart = cursor;
                points.push_back(transformPoint(cursor));
                break;

            case PathCommandType::LineTo:
                cursor = {cmd.params[0], cmd.params[1]};
                points.push_back(transformPoint(cursor));
                break;

            case PathCommandType::Arc: {
                float cx = cmd.params[0];
                float cy = cmd.params[1];
                float radius = cmd.params[2];
                float startAngle = cmd.params[3];
                float endAngle = cmd.params[4];
                bool ccw = cmd.params[5] > 0.5f;

                // Line to start of arc if needed
                glm::vec2 arcStart = {cx + radius * std::cos(startAngle), cy + radius * std::sin(startAngle)};
                if (points.empty() || glm::length(transformPoint(cursor) - transformPoint(arcStart)) > 0.01f) {
                    points.push_back(transformPoint(arcStart));
                }

                tessellateArc(points, cx, cy, radius, startAngle, endAngle, ccw);
                cursor = {cx + radius * std::cos(endAngle), cy + radius * std::sin(endAngle)};
                break;
            }

            case PathCommandType::ArcTo: {
                // arcTo is complex - simplified implementation
                // Just draw line to x2, y2 for now
                cursor = {cmd.params[2], cmd.params[3]};
                points.push_back(transformPoint(cursor));
                break;
            }

            case PathCommandType::QuadraticCurveTo: {
                float cpx = cmd.params[0];
                float cpy = cmd.params[1];
                float x = cmd.params[2];
                float y = cmd.params[3];
                tessellateQuadratic(points, cursor, cpx, cpy, x, y);
                cursor = {x, y};
                break;
            }

            case PathCommandType::BezierCurveTo: {
                float cp1x = cmd.params[0];
                float cp1y = cmd.params[1];
                float cp2x = cmd.params[2];
                float cp2y = cmd.params[3];
                float x = cmd.params[4];
                float y = cmd.params[5];
                tessellateBezier(points, cursor, cp1x, cp1y, cp2x, cp2y, x, y);
                cursor = {x, y};
                break;
            }

            case PathCommandType::ClosePath:
                cursor = subpathStart;
                // Don't add duplicate point if already at start
                break;
        }
    }

    return points;
}

void Canvas::generateStrokeGeometry(const std::vector<glm::vec2>& points, bool closed) {
    if (points.size() < 2) return;

    float halfWidth = m_state.lineWidth * 0.5f;
    glm::vec4 color = applyAlpha(m_state.strokeColor);

    // Generate quads for each line segment
    for (size_t i = 0; i < points.size() - 1; ++i) {
        glm::vec2 p0 = points[i];
        glm::vec2 p1 = points[i + 1];

        // Calculate perpendicular
        glm::vec2 dir = p1 - p0;
        float len = glm::length(dir);
        if (len < 0.001f) continue;
        dir /= len;
        glm::vec2 perp = {-dir.y, dir.x};

        // Generate quad vertices
        glm::vec2 v0 = p0 - perp * halfWidth;
        glm::vec2 v1 = p0 + perp * halfWidth;
        glm::vec2 v2 = p1 + perp * halfWidth;
        glm::vec2 v3 = p1 - perp * halfWidth;

        m_renderer->addSolidQuad(v0, v1, v2, v3, color);

        // Add round cap at start (first segment only)
        if (i == 0 && !closed && m_state.lineCap == LineCap::Round) {
            // Simple circle at start
            int capSegments = 8;
            for (int j = 0; j < capSegments; ++j) {
                float a0 = PI * 0.5f + PI * static_cast<float>(j) / capSegments;
                float a1 = PI * 0.5f + PI * static_cast<float>(j + 1) / capSegments;
                glm::vec2 c0 = p0 + halfWidth * glm::vec2(std::cos(a0) * (-dir.x) - std::sin(a0) * (-dir.y),
                                                          std::sin(a0) * (-dir.x) + std::cos(a0) * (-dir.y));
                glm::vec2 c1 = p0 + halfWidth * glm::vec2(std::cos(a1) * (-dir.x) - std::sin(a1) * (-dir.y),
                                                          std::sin(a1) * (-dir.x) + std::cos(a1) * (-dir.y));
                m_renderer->triangleFilled(p0, c0, c1, color);
            }
        }

        // Add line join at corner (if not last segment)
        if (i < points.size() - 2) {
            glm::vec2 p2 = points[i + 2];
            glm::vec2 nextDir = glm::normalize(p2 - p1);

            if (m_state.lineJoin == LineJoin::Round) {
                // Simple round join
                glm::vec2 nextPerp = {-nextDir.y, nextDir.x};
                int joinSegments = 4;
                for (int j = 0; j < joinSegments; ++j) {
                    float t0 = static_cast<float>(j) / joinSegments;
                    float t1 = static_cast<float>(j + 1) / joinSegments;
                    glm::vec2 j0 = glm::mix(perp, nextPerp, t0) * halfWidth;
                    glm::vec2 j1 = glm::mix(perp, nextPerp, t1) * halfWidth;
                    m_renderer->triangleFilled(p1, p1 + j0, p1 + j1, color);
                    m_renderer->triangleFilled(p1, p1 - j0, p1 - j1, color);
                }
            } else if (m_state.lineJoin == LineJoin::Bevel) {
                // Bevel join - simple triangle
                glm::vec2 nextPerp = {-nextDir.y, nextDir.x};
                m_renderer->triangleFilled(p1, p1 + perp * halfWidth, p1 + nextPerp * halfWidth, color);
                m_renderer->triangleFilled(p1, p1 - perp * halfWidth, p1 - nextPerp * halfWidth, color);
            }
            // Miter join is implicit from overlapping quads
        }
    }

    // Add round cap at end (last segment)
    if (!closed && m_state.lineCap == LineCap::Round && points.size() >= 2) {
        glm::vec2 p0 = points[points.size() - 2];
        glm::vec2 p1 = points[points.size() - 1];
        glm::vec2 dir = glm::normalize(p1 - p0);

        int capSegments = 8;
        for (int j = 0; j < capSegments; ++j) {
            float a0 = -PI * 0.5f + PI * static_cast<float>(j) / capSegments;
            float a1 = -PI * 0.5f + PI * static_cast<float>(j + 1) / capSegments;
            glm::vec2 c0 = p1 + halfWidth * glm::vec2(std::cos(a0) * dir.x - std::sin(a0) * dir.y,
                                                      std::sin(a0) * dir.x + std::cos(a0) * dir.y);
            glm::vec2 c1 = p1 + halfWidth * glm::vec2(std::cos(a1) * dir.x - std::sin(a1) * dir.y,
                                                      std::sin(a1) * dir.x + std::cos(a1) * dir.y);
            m_renderer->triangleFilled(p1, c0, c1, color);
        }
    }
}

void Canvas::fill() {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }

    auto polygon = pathToPolygon();
    if (polygon.size() < 3) return;

    // Convert to earcut format
    using Point = std::array<float, 2>;
    std::vector<std::vector<Point>> polygonData;
    std::vector<Point> ring;
    for (const auto& p : polygon) {
        ring.push_back({p.x, p.y});
    }
    polygonData.push_back(ring);

    // Triangulate
    auto indices = mapbox::earcut<uint32_t>(polygonData);

    // Add triangles to batch with per-vertex gradient colors
    bool hasGradient = m_state.fillGradient && !m_state.fillGradient->colorStops.empty();
    if (hasGradient) {
        // Sample gradient at each vertex (use untransformed positions for gradient lookup)
        // Note: polygon points are already transformed, we need original positions for gradient
        // For simplicity, use transformed positions - gradient coordinates should match canvas space
        for (size_t i = 0; i < indices.size(); i += 3) {
            // Sample gradient at each triangle vertex
            glm::vec4 c0 = getFillColorAt(polygon[indices[i]]);
            glm::vec4 c1 = getFillColorAt(polygon[indices[i + 1]]);
            glm::vec4 c2 = getFillColorAt(polygon[indices[i + 2]]);

            // Add triangle with per-vertex colors using triangleFilled with center color
            // For better gradient quality, we use the centroid color
            glm::vec2 centroid = (polygon[indices[i]] + polygon[indices[i + 1]] + polygon[indices[i + 2]]) / 3.0f;
            glm::vec4 avgColor = (c0 + c1 + c2) / 3.0f;
            m_renderer->triangleFilled(
                polygon[indices[i]],
                polygon[indices[i + 1]],
                polygon[indices[i + 2]],
                avgColor
            );
        }
    } else {
        glm::vec4 color = applyAlpha(m_state.fillColor);
        for (size_t i = 0; i < indices.size(); i += 3) {
            m_renderer->triangleFilled(
                polygon[indices[i]],
                polygon[indices[i + 1]],
                polygon[indices[i + 2]],
                color
            );
        }
    }
}

void Canvas::stroke() {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }

    auto polygon = pathToPolygon();
    if (polygon.size() < 2) return;

    // Check if path was closed
    bool closed = false;
    for (const auto& cmd : m_currentPath) {
        if (cmd.type == PathCommandType::ClosePath) {
            closed = true;
            break;
        }
    }

    generateStrokeGeometry(polygon, closed);
}

// -------------------------------------------------------------------------
// Clipping
// -------------------------------------------------------------------------

void Canvas::clip() {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }

    // pathToPolygon() already returns transformed points
    auto polygon = pathToPolygon();
    if (polygon.size() < 3) return;

    // Tessellate using earcut
    using Point = std::array<float, 2>;
    std::vector<std::vector<Point>> inputPolygon;
    inputPolygon.resize(1);
    inputPolygon[0].reserve(polygon.size());

    for (const auto& p : polygon) {
        inputPolygon[0].push_back({p.x, p.y});
    }

    std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(inputPolygon);
    if (indices.empty()) return;

    // Increment clip depth
    m_state.clipDepth++;
    m_renderer->setClipDepth(m_state.clipDepth);

    // Add clip geometry to renderer (polygon is already transformed)
    m_renderer->addClip(polygon, indices);
}

void Canvas::resetClip() {
    m_state.clipDepth = 0;
    m_renderer->setClipDepth(0);
}

// -------------------------------------------------------------------------
// Convenience Methods
// -------------------------------------------------------------------------

void Canvas::fillRect(float x, float y, float w, float h) {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }

    // Get untransformed corner positions for gradient sampling
    glm::vec2 c0{x, y};
    glm::vec2 c1{x + w, y};
    glm::vec2 c2{x + w, y + h};
    glm::vec2 c3{x, y + h};

    // Transform corners for rendering
    glm::vec2 p0 = transformPoint(c0);
    glm::vec2 p1 = transformPoint(c1);
    glm::vec2 p2 = transformPoint(c2);
    glm::vec2 p3 = transformPoint(c3);

    // Check if we have a gradient
    bool hasGradient = m_state.fillGradient && !m_state.fillGradient->colorStops.empty();
    if (hasGradient) {
        // Sample gradient at each corner (use untransformed positions)
        glm::vec4 col0 = getFillColorAt(c0);
        glm::vec4 col1 = getFillColorAt(c1);
        glm::vec4 col2 = getFillColorAt(c2);
        glm::vec4 col3 = getFillColorAt(c3);

        // Split into two triangles with averaged colors
        // Triangle 1: p0, p1, p2
        glm::vec4 avg1 = (col0 + col1 + col2) / 3.0f;
        m_renderer->triangleFilled(p0, p1, p2, avg1);

        // Triangle 2: p0, p2, p3
        glm::vec4 avg2 = (col0 + col2 + col3) / 3.0f;
        m_renderer->triangleFilled(p0, p2, p3, avg2);
    } else {
        m_renderer->addSolidQuad(p0, p1, p2, p3, applyAlpha(m_state.fillColor));
    }
}

void Canvas::strokeRect(float x, float y, float w, float h) {
    beginPath();
    pathRect(x, y, w, h);
    stroke();
}

void Canvas::clearRect(float x, float y, float w, float h) {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }

    glm::vec2 p0 = transformPoint({x, y});
    glm::vec2 p1 = transformPoint({x + w, y});
    glm::vec2 p2 = transformPoint({x + w, y + h});
    glm::vec2 p3 = transformPoint({x, y + h});

    // Draw transparent quad
    m_renderer->addSolidQuad(p0, p1, p2, p3, {0, 0, 0, 0});
}

void Canvas::fillCircle(float x, float y, float radius, int segments) {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }

    glm::vec2 centerOrig{x, y};
    glm::vec2 center = transformPoint(centerOrig);

    bool hasGradient = m_state.fillGradient && !m_state.fillGradient->colorStops.empty();

    // Generate fan triangles
    for (int i = 0; i < segments; ++i) {
        float a0 = TAU * static_cast<float>(i) / segments;
        float a1 = TAU * static_cast<float>(i + 1) / segments;

        glm::vec2 orig0{x + radius * std::cos(a0), y + radius * std::sin(a0)};
        glm::vec2 orig1{x + radius * std::cos(a1), y + radius * std::sin(a1)};
        glm::vec2 p0 = transformPoint(orig0);
        glm::vec2 p1 = transformPoint(orig1);

        if (hasGradient) {
            // Sample gradient at each vertex
            glm::vec4 colC = getFillColorAt(centerOrig);
            glm::vec4 col0 = getFillColorAt(orig0);
            glm::vec4 col1 = getFillColorAt(orig1);
            glm::vec4 avgColor = (colC + col0 + col1) / 3.0f;
            m_renderer->triangleFilled(center, p0, p1, avgColor);
        } else {
            glm::vec4 color = applyAlpha(m_state.fillColor);
            m_renderer->triangleFilled(center, p0, p1, color);
        }
    }
}

void Canvas::strokeCircle(float x, float y, float radius, int segments) {
    beginPath();
    arc(x, y, radius, 0, TAU, false);
    closePath();
    stroke();
}

// -------------------------------------------------------------------------
// Image Drawing
// -------------------------------------------------------------------------

void Canvas::drawImage(Operator& source, float dx, float dy) {
    // Get dimensions from source operator
    auto* texOp = dynamic_cast<TextureOperator*>(&source);
    if (!texOp || !texOp->outputView()) {
        std::cerr << "[Canvas::drawImage] Warning: source operator has no texture output\n";
        return;
    }

    int srcW = texOp->outputWidth();
    int srcH = texOp->outputHeight();
    drawImage(source, dx, dy, static_cast<float>(srcW), static_cast<float>(srcH));
}

void Canvas::drawImage(Operator& source, float dx, float dy, float dw, float dh) {
    auto* texOp = dynamic_cast<TextureOperator*>(&source);
    if (!texOp || !texOp->outputView()) {
        std::cerr << "[Canvas::drawImage] Warning: source operator has no texture output\n";
        return;
    }

    int srcW = texOp->outputWidth();
    int srcH = texOp->outputHeight();
    drawImage(source, 0, 0, static_cast<float>(srcW), static_cast<float>(srcH), dx, dy, dw, dh);
}

void Canvas::drawImage(Operator& source,
                       float sx, float sy, float sw, float sh,
                       float dx, float dy, float dw, float dh) {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }

    auto* texOp = dynamic_cast<TextureOperator*>(&source);
    if (!texOp || !texOp->outputView()) {
        std::cerr << "[Canvas::drawImage] Warning: source operator has no texture output\n";
        return;
    }

    // Transform destination coordinates
    glm::vec2 p0 = transformPoint({dx, dy});
    glm::vec2 p1 = transformPoint({dx + dw, dy});
    glm::vec2 p2 = transformPoint({dx + dw, dy + dh});
    glm::vec2 p3 = transformPoint({dx, dy + dh});

    // Calculate actual dest rect after transform (for now, use axis-aligned bounding box)
    float minX = std::min({p0.x, p1.x, p2.x, p3.x});
    float maxX = std::max({p0.x, p1.x, p2.x, p3.x});
    float minY = std::min({p0.y, p1.y, p2.y, p3.y});
    float maxY = std::max({p0.y, p1.y, p2.y, p3.y});

    int srcW = texOp->outputWidth();
    int srcH = texOp->outputHeight();

    m_renderer->addImage(
        texOp->outputView(),
        srcW, srcH,
        sx, sy, sw, sh,
        minX, minY, maxX - minX, maxY - minY,
        m_state.globalAlpha
    );
}

// -------------------------------------------------------------------------
// Frame Control
// -------------------------------------------------------------------------

void Canvas::clear(float r, float g, float b, float a) {
    m_clearColor = glm::vec4(r, g, b, a);
    m_frameBegun = true;
    m_renderer->begin(m_width, m_height, m_clearColor);
    markDirty();
}

// -------------------------------------------------------------------------
// Text Rendering
// -------------------------------------------------------------------------

void Canvas::textAlign(TextAlign align) {
    m_state.textAlign = align;
}

void Canvas::textBaseline(TextBaseline baseline) {
    m_state.textBaseline = baseline;
}

void Canvas::fillText(const std::string& str, float x, float y, float letterSpacing) {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }
    if (!m_font || !m_font->valid()) {
        static int warnCount = 0;
        if (warnCount++ < 5) {
            std::cerr << "[Canvas::fillText] Warning: font not valid for text '" << str << "'\n";
        }
        return;
    }

    // Measure text for alignment calculations
    glm::vec2 size = m_font->measureText(str);
    size.x += letterSpacing * (str.length() > 0 ? str.length() - 1 : 0);

    // Apply horizontal alignment
    float drawX = x;
    switch (m_state.textAlign) {
        case TextAlign::Left:
        case TextAlign::Start:  // Assuming LTR
            break;
        case TextAlign::Right:
        case TextAlign::End:    // Assuming LTR
            drawX = x - size.x;
            break;
        case TextAlign::Center:
            drawX = x - size.x / 2.0f;
            break;
    }

    // Apply vertical baseline alignment
    // FontAtlas renders at baseline by default
    float drawY = y;
    float ascent = m_font->ascent();
    float descent = m_font->descent();

    switch (m_state.textBaseline) {
        case TextBaseline::Alphabetic:
            // Default - y is the baseline
            break;
        case TextBaseline::Top:
            drawY = y + ascent;
            break;
        case TextBaseline::Hanging:
            drawY = y + ascent * 0.8f;  // Approximate hanging baseline
            break;
        case TextBaseline::Middle:
            drawY = y + (ascent + descent) / 2.0f;
            break;
        case TextBaseline::Ideographic:
            drawY = y + descent;  // Bottom of ideographic characters
            break;
        case TextBaseline::Bottom:
            drawY = y + descent;
            break;
    }

    // Transform and render
    glm::vec2 pos = transformPoint({drawX, drawY});
    m_renderer->text(*m_font, str, pos.x, pos.y, applyAlpha(m_state.fillColor), letterSpacing);
}

void Canvas::fillTextCentered(const std::string& str, float x, float y, float letterSpacing) {
    if (!m_font || !m_font->valid()) return;

    // Save current alignment
    TextAlign savedAlign = m_state.textAlign;
    TextBaseline savedBaseline = m_state.textBaseline;

    // Temporarily set centered alignment
    m_state.textAlign = TextAlign::Center;
    m_state.textBaseline = TextBaseline::Middle;

    fillText(str, x, y, letterSpacing);

    // Restore
    m_state.textAlign = savedAlign;
    m_state.textBaseline = savedBaseline;
}

glm::vec2 Canvas::measureText(const std::string& str) const {
    if (!m_font || !m_font->valid()) {
        return glm::vec2(0);
    }
    return m_font->measureText(str);
}

TextMetrics Canvas::measureTextMetrics(const std::string& str) const {
    TextMetrics metrics;
    if (!m_font || !m_font->valid()) {
        return metrics;
    }

    glm::vec2 size = m_font->measureText(str);
    float ascent = m_font->ascent();
    float descent = m_font->descent();

    metrics.width = size.x;
    metrics.actualBoundingBoxLeft = 0;  // Simplified - would need per-glyph data
    metrics.actualBoundingBoxRight = size.x;
    metrics.actualBoundingBoxAscent = ascent;
    metrics.actualBoundingBoxDescent = -descent;  // HTML Canvas uses positive descent
    metrics.fontBoundingBoxAscent = ascent;
    metrics.fontBoundingBoxDescent = -descent;

    return metrics;
}

// -------------------------------------------------------------------------
// Operator Interface
// -------------------------------------------------------------------------

void Canvas::init(Context& ctx) {
    createOutput(ctx, m_width, m_height);

    if (!m_renderer->init(ctx)) {
        std::cerr << "[Canvas] Failed to initialize renderer\n";
        return;
    }

    m_initialized = true;
}

void Canvas::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    // Auto-begin frame if user didn't call clear()
    if (!m_frameBegun) {
        m_renderer->begin(m_width, m_height, m_clearColor);
    }

    // Render all batched primitives to our output texture
    m_renderer->render(ctx, m_output, m_outputView);

    // Reset for next frame
    m_frameBegun = false;
    didCook();
}

void Canvas::cleanup() {
    if (m_renderer) {
        m_renderer->cleanup();
    }
    if (m_font) {
        m_font->cleanup();
    }
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
