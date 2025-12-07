#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vivid::render3d {

/// 3D perspective camera
class Camera3D {
public:
    Camera3D() = default;

    // -------------------------------------------------------------------------
    /// @name Position and Orientation
    /// @{

    /// Set camera position
    Camera3D& position(glm::vec3 pos);

    /// Set look-at target
    Camera3D& target(glm::vec3 t);

    /// Set up vector
    Camera3D& up(glm::vec3 u);

    /// Set position, target, and up in one call
    Camera3D& lookAt(glm::vec3 pos, glm::vec3 target, glm::vec3 up = glm::vec3(0, 1, 0));

    /// Orbit around origin at distance with azimuth and elevation angles (radians)
    Camera3D& orbit(float distance, float azimuth, float elevation);

    /// Orbit around a specific center point
    Camera3D& orbit(glm::vec3 center, float distance, float azimuth, float elevation);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Projection
    /// @{

    /// Set vertical field of view in degrees
    Camera3D& fov(float degrees);

    /// Set near clip plane
    Camera3D& nearPlane(float n);

    /// Set far clip plane
    Camera3D& farPlane(float f);

    /// Set aspect ratio (width / height)
    Camera3D& aspect(float a);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Computed Matrices
    /// @{

    /// Get view matrix
    glm::mat4 viewMatrix() const;

    /// Get projection matrix
    glm::mat4 projectionMatrix() const;

    /// Get combined view-projection matrix
    glm::mat4 viewProjectionMatrix() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Accessors
    /// @{

    glm::vec3 getPosition() const { return m_position; }
    glm::vec3 getTarget() const { return m_target; }
    glm::vec3 getUp() const { return m_up; }
    float getFov() const { return m_fov; }
    float getNear() const { return m_near; }
    float getFar() const { return m_far; }
    float getAspect() const { return m_aspect; }

    /// Get forward direction (normalized)
    glm::vec3 forward() const;

    /// Get right direction (normalized)
    glm::vec3 right() const;

    /// @}

private:
    glm::vec3 m_position = glm::vec3(0, 0, 5);
    glm::vec3 m_target = glm::vec3(0, 0, 0);
    glm::vec3 m_up = glm::vec3(0, 1, 0);
    float m_fov = 45.0f;
    float m_near = 0.1f;
    float m_far = 100.0f;
    float m_aspect = 16.0f / 9.0f;
};

} // namespace vivid::render3d
