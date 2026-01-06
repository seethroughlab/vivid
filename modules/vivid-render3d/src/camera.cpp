#include <vivid/render3d/camera.h>
#include <cmath>

namespace vivid::render3d {

// ============================================================================
// Frustum Implementation
// ============================================================================

void Frustum::extractFromMatrix(const glm::mat4& vp) {
    // Extract frustum planes from view-projection matrix
    // Using Gribb/Hartmann method (Fast Extraction of Viewing Frustum Planes)
    // Each row of the matrix gives us plane coefficients

    // Left plane:   row3 + row0
    m_planes[0] = glm::vec4(
        vp[0][3] + vp[0][0],
        vp[1][3] + vp[1][0],
        vp[2][3] + vp[2][0],
        vp[3][3] + vp[3][0]
    );

    // Right plane:  row3 - row0
    m_planes[1] = glm::vec4(
        vp[0][3] - vp[0][0],
        vp[1][3] - vp[1][0],
        vp[2][3] - vp[2][0],
        vp[3][3] - vp[3][0]
    );

    // Bottom plane: row3 + row1
    m_planes[2] = glm::vec4(
        vp[0][3] + vp[0][1],
        vp[1][3] + vp[1][1],
        vp[2][3] + vp[2][1],
        vp[3][3] + vp[3][1]
    );

    // Top plane:    row3 - row1
    m_planes[3] = glm::vec4(
        vp[0][3] - vp[0][1],
        vp[1][3] - vp[1][1],
        vp[2][3] - vp[2][1],
        vp[3][3] - vp[3][1]
    );

    // Near plane:   row3 + row2
    m_planes[4] = glm::vec4(
        vp[0][3] + vp[0][2],
        vp[1][3] + vp[1][2],
        vp[2][3] + vp[2][2],
        vp[3][3] + vp[3][2]
    );

    // Far plane:    row3 - row2
    m_planes[5] = glm::vec4(
        vp[0][3] - vp[0][2],
        vp[1][3] - vp[1][2],
        vp[2][3] - vp[2][2],
        vp[3][3] - vp[3][2]
    );

    // Normalize all planes
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(m_planes[i]));
        if (len > 0.0001f) {
            m_planes[i] /= len;
        }
    }
}

bool Frustum::intersectsSphere(const glm::vec3& center, float radius) const {
    // Check sphere against all 6 frustum planes
    // If sphere is completely outside any plane, it's outside the frustum
    for (int i = 0; i < 6; i++) {
        float distance = glm::dot(glm::vec3(m_planes[i]), center) + m_planes[i].w;
        if (distance < -radius) {
            return false;  // Sphere is completely outside this plane
        }
    }
    return true;  // Sphere intersects or is inside frustum
}

bool Frustum::containsPoint(const glm::vec3& point) const {
    for (int i = 0; i < 6; i++) {
        float distance = glm::dot(glm::vec3(m_planes[i]), point) + m_planes[i].w;
        if (distance < 0.0f) {
            return false;  // Point is outside this plane
        }
    }
    return true;
}

// ============================================================================
// Camera3D Implementation
// ============================================================================

void Camera3D::position(glm::vec3 pos) {
    m_position = pos;
}

void Camera3D::target(glm::vec3 t) {
    m_target = t;
}

void Camera3D::up(glm::vec3 u) {
    m_up = u;
}

void Camera3D::lookAt(glm::vec3 pos, glm::vec3 target, glm::vec3 up) {
    m_position = pos;
    m_target = target;
    m_up = up;
}

void Camera3D::orbit(float distance, float azimuth, float elevation) {
    orbit(glm::vec3(0), distance, azimuth, elevation);
}

void Camera3D::orbit(glm::vec3 center, float distance, float azimuth, float elevation) {
    // Clamp elevation to avoid gimbal lock
    elevation = glm::clamp(elevation, -glm::half_pi<float>() * 0.99f,
                                       glm::half_pi<float>() * 0.99f);

    float cosElev = std::cos(elevation);
    float sinElev = std::sin(elevation);
    float cosAz = std::cos(azimuth);
    float sinAz = std::sin(azimuth);

    m_position = center + glm::vec3(
        distance * cosElev * sinAz,
        distance * sinElev,
        distance * cosElev * cosAz
    );

    m_target = center;
    m_up = glm::vec3(0, 1, 0);
}

void Camera3D::fov(float degrees) {
    m_fov = degrees;
}

void Camera3D::nearPlane(float n) {
    m_near = n;
}

void Camera3D::farPlane(float f) {
    m_far = f;
}

void Camera3D::aspect(float a) {
    m_aspect = a;
}

void Camera3D::projectionMode(ProjectionMode mode) {
    m_projectionMode = mode;
}

void Camera3D::orthoSize(float size) {
    m_orthoSize = size;
}

glm::mat4 Camera3D::viewMatrix() const {
    return glm::lookAt(m_position, m_target, m_up);
}

glm::mat4 Camera3D::projectionMatrix() const {
    if (m_projectionMode == ProjectionMode::Orthographic) {
        float halfH = m_orthoSize * 0.5f;
        float halfW = halfH * m_aspect;
        return glm::ortho(-halfW, halfW, -halfH, halfH, m_near, m_far);
    }
    return glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
}

glm::mat4 Camera3D::viewProjectionMatrix() const {
    return projectionMatrix() * viewMatrix();
}

glm::vec3 Camera3D::forward() const {
    return glm::normalize(m_target - m_position);
}

glm::vec3 Camera3D::right() const {
    return glm::normalize(glm::cross(forward(), m_up));
}

} // namespace vivid::render3d
