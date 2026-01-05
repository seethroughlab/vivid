#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vivid::render3d {

/// Frustum for view frustum culling
class Frustum {
public:
    /// Extract frustum planes from view-projection matrix
    void extractFromMatrix(const glm::mat4& viewProj);

    /// Test if a sphere intersects the frustum
    /// @param center Sphere center in world space
    /// @param radius Sphere radius
    /// @return true if sphere is inside or intersects frustum
    bool intersectsSphere(const glm::vec3& center, float radius) const;

    /// Test if a point is inside the frustum
    bool containsPoint(const glm::vec3& point) const;

private:
    // Frustum planes: left, right, bottom, top, near, far
    // Each plane is (A, B, C, D) where Ax + By + Cz + D = 0
    // Normal points inward (positive side is inside frustum)
    glm::vec4 m_planes[6];
};

/// Camera projection mode
enum class ProjectionMode {
    Perspective,    ///< Standard perspective projection (FOV-based)
    Orthographic    ///< Orthographic projection (no perspective distortion)
};

/// 3D camera with perspective or orthographic projection
class Camera3D {
public:
    Camera3D() = default;

    // -------------------------------------------------------------------------
    /// @name Position and Orientation
    /// @{

    /// Set camera position
    void position(glm::vec3 pos);

    /// Set look-at target
    void target(glm::vec3 t);

    /// Set up vector
    void up(glm::vec3 u);

    /// Set position, target, and up in one call
    void lookAt(glm::vec3 pos, glm::vec3 target, glm::vec3 up = glm::vec3(0, 1, 0));

    /// Orbit around origin at distance with azimuth and elevation angles (radians)
    void orbit(float distance, float azimuth, float elevation);

    /// Orbit around a specific center point
    void orbit(glm::vec3 center, float distance, float azimuth, float elevation);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Projection
    /// @{

    /// Set vertical field of view in degrees
    void fov(float degrees);

    /// Set near clip plane
    void nearPlane(float n);

    /// Set far clip plane
    void farPlane(float f);

    /// Set aspect ratio (width / height)
    void aspect(float a);

    /// Set projection mode (Perspective or Orthographic)
    void projectionMode(ProjectionMode mode);

    /// Set orthographic size (vertical extent in world units)
    /// Only used when projectionMode is Orthographic
    void orthoSize(float size);

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
    ProjectionMode getProjectionMode() const { return m_projectionMode; }
    float getOrthoSize() const { return m_orthoSize; }

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
    ProjectionMode m_projectionMode = ProjectionMode::Perspective;
    float m_orthoSize = 10.0f;  ///< Vertical extent for orthographic projection
};

} // namespace vivid::render3d
