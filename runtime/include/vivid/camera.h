#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vivid {

/// 3D camera with perspective projection and orbit controls
class Camera3D {
public:
    Camera3D();

    // --- Projection ---

    /// Set perspective projection
    /// @param fovDegrees Vertical field of view in degrees
    /// @param aspectRatio Width / height ratio
    /// @param nearPlane Near clipping plane distance
    /// @param farPlane Far clipping plane distance
    void setPerspective(float fovDegrees, float aspectRatio, float nearPlane = 0.1f, float farPlane = 1000.0f);

    /// Update aspect ratio (e.g., on window resize)
    void setAspectRatio(float aspectRatio);

    /// Get projection matrix
    const glm::mat4& projectionMatrix() const { return projectionMatrix_; }

    // --- View (Position & Orientation) ---

    /// Set camera position and look target
    void lookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up = glm::vec3(0, 1, 0));

    /// Set camera position directly
    void setPosition(const glm::vec3& position);

    /// Set camera rotation (euler angles in degrees)
    void setRotation(float pitch, float yaw, float roll = 0.0f);

    /// Get camera position
    const glm::vec3& position() const { return position_; }

    /// Get camera target point
    const glm::vec3& target() const { return target_; }

    /// Get forward direction (normalized)
    glm::vec3 forward() const;

    /// Get right direction (normalized)
    glm::vec3 right() const;

    /// Get up direction (normalized)
    glm::vec3 up() const;

    /// Get view matrix
    const glm::mat4& viewMatrix() const { return viewMatrix_; }

    /// Get combined view-projection matrix
    glm::mat4 viewProjectionMatrix() const { return projectionMatrix_ * viewMatrix_; }

    // --- Orbit Controls ---

    /// Set orbit parameters
    /// @param target Center point to orbit around
    /// @param distance Distance from target
    /// @param azimuth Horizontal angle in degrees (0 = +X, 90 = +Z)
    /// @param elevation Vertical angle in degrees (-90 = bottom, 0 = horizon, 90 = top)
    void setOrbit(const glm::vec3& target, float distance, float azimuth, float elevation);

    /// Get current orbit distance
    float orbitDistance() const { return orbitDistance_; }

    /// Get current orbit azimuth angle (degrees)
    float orbitAzimuth() const { return orbitAzimuth_; }

    /// Get current orbit elevation angle (degrees)
    float orbitElevation() const { return orbitElevation_; }

    /// Rotate orbit (add to azimuth and elevation)
    void orbitRotate(float deltaAzimuth, float deltaElevation);

    /// Zoom orbit (multiply distance)
    void orbitZoom(float factor);

    /// Pan orbit target
    void orbitPan(float deltaX, float deltaY);

    // --- Utility ---

    /// Convert world position to screen position (NDC)
    glm::vec3 worldToScreen(const glm::vec3& worldPos) const;

    /// Get a ray from camera through screen point
    /// @param screenX Screen X normalized (0 to 1)
    /// @param screenY Screen Y normalized (0 to 1)
    /// @return Ray direction (normalized)
    glm::vec3 screenToRay(float screenX, float screenY) const;

private:
    void updateViewMatrix();
    void updateProjectionMatrix();
    void updateOrbitPosition();

    // Projection parameters
    float fovDegrees_ = 60.0f;
    float aspectRatio_ = 16.0f / 9.0f;
    float nearPlane_ = 0.1f;
    float farPlane_ = 1000.0f;

    // View parameters
    glm::vec3 position_{0, 0, 5};
    glm::vec3 target_{0, 0, 0};
    glm::vec3 worldUp_{0, 1, 0};

    // Orbit parameters
    float orbitDistance_ = 5.0f;
    float orbitAzimuth_ = 0.0f;    // degrees
    float orbitElevation_ = 30.0f; // degrees

    // Cached matrices
    glm::mat4 viewMatrix_{1.0f};
    glm::mat4 projectionMatrix_{1.0f};

    bool useOrbit_ = false;
};

} // namespace vivid
