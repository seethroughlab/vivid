#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vivid {

/**
 * @brief 3D perspective camera with view and projection matrices.
 *
 * Provides look-at view matrix and perspective projection.
 * Use for rendering 3D scenes to texture.
 */
class Camera3D {
public:
    Camera3D() = default;

    // Transform
    glm::vec3 position{0.0f, 0.0f, 5.0f};  // Camera position in world space
    glm::vec3 target{0.0f, 0.0f, 0.0f};    // Look-at target point
    glm::vec3 up{0.0f, 1.0f, 0.0f};        // Up vector

    // Projection
    float fov = 60.0f;          // Vertical field of view in degrees
    float nearPlane = 0.1f;     // Near clipping plane
    float farPlane = 1000.0f;   // Far clipping plane

    /**
     * @brief Get the view matrix (world-to-camera transform).
     */
    glm::mat4 viewMatrix() const {
        return glm::lookAt(position, target, up);
    }

    /**
     * @brief Get the projection matrix.
     * @param aspectRatio Width / height of the render target.
     */
    glm::mat4 projectionMatrix(float aspectRatio) const {
        return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    }

    /**
     * @brief Get the combined view-projection matrix.
     * @param aspectRatio Width / height of the render target.
     */
    glm::mat4 viewProjectionMatrix(float aspectRatio) const {
        return projectionMatrix(aspectRatio) * viewMatrix();
    }

    /**
     * @brief Get the camera's forward direction (normalized).
     */
    glm::vec3 forward() const {
        return glm::normalize(target - position);
    }

    /**
     * @brief Get the camera's right direction (normalized).
     */
    glm::vec3 right() const {
        return glm::normalize(glm::cross(forward(), up));
    }

    /**
     * @brief Move the camera by an offset in world space.
     */
    void translate(const glm::vec3& offset) {
        position += offset;
        target += offset;
    }

    /**
     * @brief Orbit the camera around the target point.
     * @param yawDelta Horizontal rotation in radians.
     * @param pitchDelta Vertical rotation in radians.
     */
    void orbit(float yawDelta, float pitchDelta) {
        // Calculate the offset from target
        glm::vec3 offset = position - target;
        float distance = glm::length(offset);

        // Convert to spherical coordinates
        float theta = std::atan2(offset.x, offset.z);  // Yaw
        float phi = std::acos(offset.y / distance);     // Pitch

        // Apply rotation
        theta += yawDelta;
        phi = glm::clamp(phi + pitchDelta, 0.01f, 3.13f);  // Clamp to avoid gimbal lock

        // Convert back to Cartesian
        position = target + glm::vec3(
            distance * std::sin(phi) * std::sin(theta),
            distance * std::cos(phi),
            distance * std::sin(phi) * std::cos(theta)
        );
    }

    /**
     * @brief Zoom the camera (change distance to target).
     * @param delta Positive zooms in, negative zooms out.
     */
    void zoom(float delta) {
        glm::vec3 offset = position - target;
        float distance = glm::length(offset);
        float newDistance = glm::max(0.1f, distance - delta);
        position = target + glm::normalize(offset) * newDistance;
    }

    /**
     * @brief Get distance from camera to target.
     */
    float distanceToTarget() const {
        return glm::length(position - target);
    }
};

/**
 * @brief Camera uniform buffer layout for shaders.
 *
 * Must match the WGSL struct layout exactly.
 * Total size: 208 bytes (aligned to 16 bytes)
 */
struct CameraUniform {
    glm::mat4 view;             // 64 bytes
    glm::mat4 projection;       // 64 bytes
    glm::mat4 viewProjection;   // 64 bytes
    glm::vec3 cameraPosition;   // 12 bytes
    float _pad;                 // 4 bytes (padding for alignment)
};

/**
 * @brief Fill a CameraUniform struct from a Camera3D.
 * @param camera The camera to extract matrices from.
 * @param aspectRatio Width / height of render target.
 * @return Filled CameraUniform ready for GPU upload.
 */
inline CameraUniform makeCameraUniform(const Camera3D& camera, float aspectRatio) {
    CameraUniform u;
    u.view = camera.viewMatrix();
    u.projection = camera.projectionMatrix(aspectRatio);
    u.viewProjection = camera.viewProjectionMatrix(aspectRatio);
    u.cameraPosition = camera.position;
    u._pad = 0.0f;
    return u;
}

} // namespace vivid
