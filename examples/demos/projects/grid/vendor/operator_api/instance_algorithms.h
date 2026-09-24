#pragma once

#include <cmath>
#include <cstdint>

// =============================================================================
// instance_algorithms.h — shared layout math for 2D and 3D instancing
// =============================================================================
//
// Header-only. Used by InstanceGrid2D (core), InstanceGrid (vivid-3d), and
// Instancer3D's legacy lane-path to avoid duplicating the grid/circle/line
// formulas in three places. All layouts centre at origin; callers map the
// returned 2D coordinates onto whichever plane or axes they need (2D uses
// XY; 3D typically maps to XZ with y=0 for a floor-plane grid).
// =============================================================================

namespace vivid::instancing {

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

// Grid: ceil(sqrt(n)) columns, centred at origin, row-major by index.
inline Vec2 grid_2d(uint32_t i, uint32_t count, float spacing) {
    uint32_t cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(count))));
    if (cols == 0) cols = 1;
    uint32_t rows = (count + cols - 1) / cols;
    float ox = -static_cast<float>(cols - 1) * spacing * 0.5f;
    float oy = -static_cast<float>(rows - 1) * spacing * 0.5f;
    uint32_t col = i % cols;
    uint32_t row = i / cols;
    return { ox + static_cast<float>(col) * spacing,
             oy + static_cast<float>(row) * spacing };
}

// Circle: evenly-spaced ring. Radius scales with count so spacing ≈ arc length.
inline Vec2 circle_2d(uint32_t i, uint32_t count, float spacing) {
    constexpr float kTau = 6.28318530718f;
    float n = static_cast<float>(count);
    float angle = kTau * static_cast<float>(i) / (n > 0.0f ? n : 1.0f);
    float radius = spacing * n / kTau;
    if (radius < spacing) radius = spacing;
    return { radius * std::cos(angle), radius * std::sin(angle) };
}

// Line: horizontal row centred at origin along the x axis.
inline Vec2 line_2d(uint32_t i, uint32_t count, float spacing) {
    float total = (count > 1) ? spacing * static_cast<float>(count - 1) : 0.0f;
    float start = -total * 0.5f;
    return { start + spacing * static_cast<float>(i), 0.0f };
}

// Grid3D: cubic lattice dim = ceil(cbrt(n)), centred at origin.
inline Vec3 grid_3d(uint32_t i, uint32_t count, float spacing) {
    uint32_t dim = static_cast<uint32_t>(std::ceil(std::cbrt(static_cast<float>(count))));
    if (dim == 0) dim = 1;
    float ofs = -static_cast<float>(dim - 1) * spacing * 0.5f;
    uint32_t xi = i % dim;
    uint32_t yi = (i / dim) % dim;
    uint32_t zi = i / (dim * dim);
    return { ofs + static_cast<float>(xi) * spacing,
             ofs + static_cast<float>(yi) * spacing,
             ofs + static_cast<float>(zi) * spacing };
}

} // namespace vivid::instancing
