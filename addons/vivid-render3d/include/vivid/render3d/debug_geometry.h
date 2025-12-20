#pragma once

/**
 * @file debug_geometry.h
 * @brief Debug wireframe geometry generation utilities
 *
 * Helper functions for generating wireframe debug geometry for
 * cameras and lights. Used by Render3D::renderDebugVisualization().
 */

#include <vivid/render3d/mesh.h>
#include <vivid/render3d/camera.h>
#include <vivid/render3d/light_operators.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>

namespace vivid::render3d {

// Helper to create a line vertex for debug wireframes
inline Vertex3D makeDebugVertex(const glm::vec3& pos, const glm::vec4& color) {
    Vertex3D v;
    v.position = pos;
    v.normal = glm::vec3(0, 1, 0);
    v.tangent = glm::vec4(1, 0, 0, 1);
    v.uv = glm::vec2(0, 0);
    v.color = color;
    return v;
}

// Add a line segment to the debug vertices
inline void addLine(std::vector<Vertex3D>& verts, const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) {
    verts.push_back(makeDebugVertex(a, color));
    verts.push_back(makeDebugVertex(b, color));
}

// Generate camera frustum wireframe (12 lines connecting 8 corners)
inline void generateCameraFrustum(std::vector<Vertex3D>& verts, const Camera3D& camera, const glm::vec4& color) {
    // Get inverse view-projection to transform NDC corners to world space
    glm::mat4 invVP = glm::inverse(camera.projectionMatrix() * camera.viewMatrix());

    // NDC corners (near plane at z=-1, far plane at z=1 in NDC)
    glm::vec4 ndcCorners[8] = {
        {-1, -1, -1, 1}, { 1, -1, -1, 1}, { 1,  1, -1, 1}, {-1,  1, -1, 1},  // near
        {-1, -1,  1, 1}, { 1, -1,  1, 1}, { 1,  1,  1, 1}, {-1,  1,  1, 1}   // far
    };

    // Transform to world space
    glm::vec3 corners[8];
    for (int i = 0; i < 8; i++) {
        glm::vec4 world = invVP * ndcCorners[i];
        corners[i] = glm::vec3(world) / world.w;
    }

    // Near plane edges
    addLine(verts, corners[0], corners[1], color);
    addLine(verts, corners[1], corners[2], color);
    addLine(verts, corners[2], corners[3], color);
    addLine(verts, corners[3], corners[0], color);

    // Far plane edges
    addLine(verts, corners[4], corners[5], color);
    addLine(verts, corners[5], corners[6], color);
    addLine(verts, corners[6], corners[7], color);
    addLine(verts, corners[7], corners[4], color);

    // Connecting edges (near to far)
    addLine(verts, corners[0], corners[4], color);
    addLine(verts, corners[1], corners[5], color);
    addLine(verts, corners[2], corners[6], color);
    addLine(verts, corners[3], corners[7], color);
}

// Generate directional light arrow (5 lines: shaft + 4 arrowhead)
inline void generateDirectionalLightDebug(std::vector<Vertex3D>& verts, const LightData& light, const glm::vec4& color) {
    glm::vec3 dir = glm::normalize(light.direction);
    float len = 2.0f;  // Arrow length

    // Arrow shaft from origin in light direction
    glm::vec3 start = glm::vec3(0, 0, 0);
    glm::vec3 end = start + dir * len;
    addLine(verts, start, end, color);

    // Create arrowhead basis vectors
    glm::vec3 up = glm::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(dir, up));
    glm::vec3 forward = glm::normalize(glm::cross(right, dir));

    // Arrowhead
    float headSize = 0.3f;
    glm::vec3 headBase = end - dir * headSize * 2.0f;
    addLine(verts, end, headBase + right * headSize, color);
    addLine(verts, end, headBase - right * headSize, color);
    addLine(verts, end, headBase + forward * headSize, color);
    addLine(verts, end, headBase - forward * headSize, color);
}

// Generate point light sphere wireframe (3 circles on XY, XZ, YZ planes)
inline void generatePointLightDebug(std::vector<Vertex3D>& verts, const LightData& light, const glm::vec4& color) {
    const int segments = 24;
    float r = light.range;
    glm::vec3 pos = light.position;

    for (int i = 0; i < segments; i++) {
        float a1 = (float)i / segments * 2.0f * 3.14159f;
        float a2 = (float)(i + 1) / segments * 2.0f * 3.14159f;

        // XY circle
        addLine(verts, pos + glm::vec3(cosf(a1) * r, sinf(a1) * r, 0),
                       pos + glm::vec3(cosf(a2) * r, sinf(a2) * r, 0), color);
        // XZ circle
        addLine(verts, pos + glm::vec3(cosf(a1) * r, 0, sinf(a1) * r),
                       pos + glm::vec3(cosf(a2) * r, 0, sinf(a2) * r), color);
        // YZ circle
        addLine(verts, pos + glm::vec3(0, cosf(a1) * r, sinf(a1) * r),
                       pos + glm::vec3(0, cosf(a2) * r, sinf(a2) * r), color);
    }
}

// Generate spot light cone wireframe (edges from apex to base circle)
inline void generateSpotLightDebug(std::vector<Vertex3D>& verts, const LightData& light, const glm::vec4& color) {
    glm::vec3 pos = light.position;
    glm::vec3 dir = glm::normalize(light.direction);
    float range = light.range;
    float angleRad = glm::radians(light.spotAngle);
    float baseRadius = tanf(angleRad) * range;

    // Create cone basis vectors
    glm::vec3 up = glm::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(dir, up));
    glm::vec3 forward = glm::normalize(glm::cross(right, dir));

    glm::vec3 apex = pos;
    glm::vec3 baseCenter = pos + dir * range;

    // Cone edges from apex to base circle
    const int edges = 8;
    for (int i = 0; i < edges; i++) {
        float angle = (float)i / edges * 2.0f * 3.14159f;
        glm::vec3 basePoint = baseCenter + (right * cosf(angle) + forward * sinf(angle)) * baseRadius;
        addLine(verts, apex, basePoint, color);
    }

    // Base circle
    const int segments = 24;
    for (int i = 0; i < segments; i++) {
        float a1 = (float)i / segments * 2.0f * 3.14159f;
        float a2 = (float)(i + 1) / segments * 2.0f * 3.14159f;
        glm::vec3 p1 = baseCenter + (right * cosf(a1) + forward * sinf(a1)) * baseRadius;
        glm::vec3 p2 = baseCenter + (right * cosf(a2) + forward * sinf(a2)) * baseRadius;
        addLine(verts, p1, p2, color);
    }
}

} // namespace vivid::render3d
