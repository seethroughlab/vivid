#pragma once

// Vivid Effects 2D - Particle Types
// GPU-aligned structs for instanced rendering

#include <glm/glm.hpp>

namespace vivid::effects {

// Circle for instanced rendering (SDF-based)
// Total size: 32 bytes (aligned for GPU)
struct Circle2D {
    glm::vec2 position;  // Normalized 0-1 screen coords
    float radius;        // Normalized radius
    float _pad = 0.0f;   // GPU alignment padding
    glm::vec4 color;     // RGBA color

    Circle2D() = default;
    Circle2D(glm::vec2 pos, float r, glm::vec4 c)
        : position(pos), radius(r), _pad(0.0f), color(c) {}
    Circle2D(float x, float y, float r, float red, float green, float blue, float alpha = 1.0f)
        : position(x, y), radius(r), _pad(0.0f), color(red, green, blue, alpha) {}
};

// Sprite particle for textured rendering
// Total size: 48 bytes (aligned for GPU)
struct Sprite2D {
    glm::vec2 position;  // Normalized 0-1 screen coords
    float size;          // Normalized size
    float rotation;      // Rotation in radians
    glm::vec4 color;     // Tint color (multiplied with texture)
    glm::vec2 uvOffset;  // UV offset for sprite sheets (default 0,0)
    glm::vec2 uvScale;   // UV scale for sprite sheets (default 1,1)

    Sprite2D() : uvOffset(0.0f), uvScale(1.0f) {}
    Sprite2D(glm::vec2 pos, float s, float rot, glm::vec4 c)
        : position(pos), size(s), rotation(rot), color(c), uvOffset(0.0f), uvScale(1.0f) {}
};

} // namespace vivid::effects
