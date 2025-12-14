#include <vivid/render3d/camera.h>
#include <cmath>

namespace vivid::render3d {

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
