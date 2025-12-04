// Camera implementation

#include "vivid/camera.h"
#include <algorithm>
#include <cmath>

namespace vivid {

constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

Camera3D::Camera3D() {
    updateProjectionMatrix();
    updateViewMatrix();
}

void Camera3D::setPerspective(float fovDegrees, float aspectRatio, float nearPlane, float farPlane) {
    fovDegrees_ = fovDegrees;
    aspectRatio_ = aspectRatio;
    nearPlane_ = nearPlane;
    farPlane_ = farPlane;
    updateProjectionMatrix();
}

void Camera3D::setAspectRatio(float aspectRatio) {
    aspectRatio_ = aspectRatio;
    updateProjectionMatrix();
}

void Camera3D::lookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up) {
    position_ = eye;
    target_ = target;
    worldUp_ = up;
    useOrbit_ = false;

    // Calculate orbit parameters from look-at
    glm::vec3 dir = position_ - target_;
    orbitDistance_ = glm::length(dir);
    if (orbitDistance_ > 0.001f) {
        dir /= orbitDistance_;
        orbitElevation_ = std::asin(dir.y) / DEG_TO_RAD;
        orbitAzimuth_ = std::atan2(dir.z, dir.x) / DEG_TO_RAD;
    }

    updateViewMatrix();
}

void Camera3D::setPosition(const glm::vec3& position) {
    position_ = position;
    useOrbit_ = false;
    updateViewMatrix();
}

void Camera3D::setRotation(float pitch, float yaw, float roll) {
    // Convert euler angles to direction
    float pitchRad = pitch * DEG_TO_RAD;
    float yawRad = yaw * DEG_TO_RAD;

    glm::vec3 forward;
    forward.x = std::cos(pitchRad) * std::cos(yawRad);
    forward.y = std::sin(pitchRad);
    forward.z = std::cos(pitchRad) * std::sin(yawRad);

    target_ = position_ + forward;
    useOrbit_ = false;
    updateViewMatrix();
}

glm::vec3 Camera3D::forward() const {
    return glm::normalize(target_ - position_);
}

glm::vec3 Camera3D::right() const {
    return glm::normalize(glm::cross(forward(), worldUp_));
}

glm::vec3 Camera3D::up() const {
    return glm::normalize(glm::cross(right(), forward()));
}

void Camera3D::setOrbit(const glm::vec3& target, float distance, float azimuth, float elevation) {
    target_ = target;
    orbitDistance_ = std::max(0.1f, distance);
    orbitAzimuth_ = azimuth;
    orbitElevation_ = std::clamp(elevation, -89.0f, 89.0f);
    useOrbit_ = true;
    updateOrbitPosition();
}

void Camera3D::orbitRotate(float deltaAzimuth, float deltaElevation) {
    orbitAzimuth_ += deltaAzimuth;
    orbitElevation_ = std::clamp(orbitElevation_ + deltaElevation, -89.0f, 89.0f);
    useOrbit_ = true;
    updateOrbitPosition();
}

void Camera3D::orbitZoom(float factor) {
    orbitDistance_ = std::max(0.1f, orbitDistance_ * factor);
    useOrbit_ = true;
    updateOrbitPosition();
}

void Camera3D::orbitPan(float deltaX, float deltaY) {
    glm::vec3 r = right();
    glm::vec3 u = up();
    target_ += r * deltaX + u * deltaY;
    useOrbit_ = true;
    updateOrbitPosition();
}

glm::vec3 Camera3D::worldToScreen(const glm::vec3& worldPos) const {
    glm::vec4 clipPos = projectionMatrix_ * viewMatrix_ * glm::vec4(worldPos, 1.0f);
    if (std::abs(clipPos.w) > 0.0001f) {
        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        return glm::vec3(
            (ndc.x + 1.0f) * 0.5f,
            (1.0f - ndc.y) * 0.5f,  // Flip Y for screen coordinates
            ndc.z
        );
    }
    return glm::vec3(0);
}

glm::vec3 Camera3D::screenToRay(float screenX, float screenY) const {
    // Convert screen to NDC
    float ndcX = screenX * 2.0f - 1.0f;
    float ndcY = 1.0f - screenY * 2.0f;  // Flip Y

    // Inverse projection
    glm::mat4 invProj = glm::inverse(projectionMatrix_);
    glm::mat4 invView = glm::inverse(viewMatrix_);

    glm::vec4 rayClip(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 rayEye = invProj * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

    glm::vec4 rayWorld = invView * rayEye;
    return glm::normalize(glm::vec3(rayWorld));
}

void Camera3D::updateViewMatrix() {
    viewMatrix_ = glm::lookAt(position_, target_, worldUp_);
}

void Camera3D::updateProjectionMatrix() {
    // Note: GLM uses OpenGL convention, Diligent uses left-handed with reversed depth
    // We'll use standard right-handed coordinates here
    projectionMatrix_ = glm::perspective(
        glm::radians(fovDegrees_),
        aspectRatio_,
        nearPlane_,
        farPlane_
    );

    // Flip Y for Vulkan (Diligent handles this internally for most cases)
    // projectionMatrix_[1][1] *= -1.0f;
}

void Camera3D::updateOrbitPosition() {
    float azimuthRad = orbitAzimuth_ * DEG_TO_RAD;
    float elevationRad = orbitElevation_ * DEG_TO_RAD;

    float cosElev = std::cos(elevationRad);
    float sinElev = std::sin(elevationRad);
    float cosAzim = std::cos(azimuthRad);
    float sinAzim = std::sin(azimuthRad);

    position_ = target_ + glm::vec3(
        orbitDistance_ * cosElev * cosAzim,
        orbitDistance_ * sinElev,
        orbitDistance_ * cosElev * sinAzim
    );

    updateViewMatrix();
}

} // namespace vivid
