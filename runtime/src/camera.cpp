#include "vivid/camera.h"

#include <algorithm>

namespace vivid {

Camera3D::Camera3D() {
    updateViewMatrix();
    updateProjectionMatrix();
}

void Camera3D::updateViewMatrix() {
    m_viewMatrix = glm::lookAt(m_position, m_target, m_up);
    m_dirty = false;
}

void Camera3D::updateProjectionMatrix() {
    // Standard right-handed perspective with Vulkan [0,1] depth range
    m_projMatrix = glm::perspectiveRH_ZO(m_fov, m_aspectRatio, m_nearPlane, m_farPlane);
    // Flip Y for Vulkan's coordinate system (Vulkan Y points down in NDC)
    // Note: Y-flip also inverts apparent triangle winding, handled by FrontCounterClockwise=False
    m_projMatrix[1][1] *= -1.0f;
    m_projDirty = false;
}

const glm::mat4& Camera3D::getViewMatrix() {
    if (m_dirty) {
        updateViewMatrix();
    }
    return m_viewMatrix;
}

const glm::mat4& Camera3D::getProjectionMatrix() {
    if (m_projDirty) {
        updateProjectionMatrix();
    }
    return m_projMatrix;
}

glm::mat4 Camera3D::getViewProjectionMatrix() {
    return getProjectionMatrix() * getViewMatrix();
}

void Camera3D::lookAt(const glm::vec3& target) {
    m_target = target;
    m_dirty = true;
}

void Camera3D::lookAt(const glm::vec3& position, const glm::vec3& target, const glm::vec3& up) {
    m_position = position;
    m_target = target;
    m_up = up;
    m_dirty = true;
}

glm::vec3 Camera3D::getForward() const {
    return glm::normalize(m_target - m_position);
}

glm::vec3 Camera3D::getRight() const {
    return glm::normalize(glm::cross(getForward(), m_up));
}

glm::vec3 Camera3D::getUpVector() const {
    return glm::normalize(glm::cross(getRight(), getForward()));
}

void Camera3D::orbit(float yawDelta, float pitchDelta) {
    // Get current offset from target
    glm::vec3 offset = m_position - m_target;
    float distance = glm::length(offset);

    // Convert to spherical coordinates
    float theta = std::atan2(offset.x, offset.z);  // Yaw
    float phi = std::asin(std::clamp(offset.y / distance, -1.0f, 1.0f));  // Pitch

    // Apply deltas
    theta += yawDelta;
    phi += pitchDelta;

    // Clamp pitch to avoid flipping
    const float limit = glm::radians(89.0f);
    phi = std::clamp(phi, -limit, limit);

    // Convert back to Cartesian
    offset.x = distance * std::cos(phi) * std::sin(theta);
    offset.y = distance * std::sin(phi);
    offset.z = distance * std::cos(phi) * std::cos(theta);

    m_position = m_target + offset;
    m_dirty = true;
}

void Camera3D::pan(float dx, float dy) {
    glm::vec3 right = getRight();
    glm::vec3 up = getUpVector();

    glm::vec3 offset = right * dx + up * dy;
    m_position += offset;
    m_target += offset;
    m_dirty = true;
}

void Camera3D::zoom(float delta) {
    // Move toward/away from target
    glm::vec3 direction = m_target - m_position;
    float distance = glm::length(direction);

    // Don't zoom past the target
    float newDistance = std::max(0.1f, distance - delta);
    direction = glm::normalize(direction);

    m_position = m_target - direction * newDistance;
    m_dirty = true;
}

void Camera3D::dolly(float delta) {
    glm::vec3 forward = getForward();
    m_position += forward * delta;
    m_target += forward * delta;
    m_dirty = true;
}

void Camera3D::setDistance(float distance) {
    glm::vec3 direction = glm::normalize(m_target - m_position);
    m_position = m_target - direction * distance;
    m_dirty = true;
}

} // namespace vivid
