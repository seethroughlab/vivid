#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vivid {

// 3D Camera with perspective projection
class Camera3D {
public:
    Camera3D();
    ~Camera3D() = default;

    // Position and orientation
    void setPosition(const glm::vec3& pos) { m_position = pos; m_dirty = true; }
    void setTarget(const glm::vec3& target) { m_target = target; m_dirty = true; }
    void setUp(const glm::vec3& up) { m_up = up; m_dirty = true; }

    const glm::vec3& getPosition() const { return m_position; }
    const glm::vec3& getTarget() const { return m_target; }
    const glm::vec3& getUp() const { return m_up; }

    // Projection settings
    void setFOV(float fovDegrees) { m_fov = glm::radians(fovDegrees); m_projDirty = true; }
    void setNearPlane(float near) { m_nearPlane = near; m_projDirty = true; }
    void setFarPlane(float far) { m_farPlane = far; m_projDirty = true; }
    void setAspectRatio(float aspect) { m_aspectRatio = aspect; m_projDirty = true; }

    float getFOV() const { return glm::degrees(m_fov); }
    float getNearPlane() const { return m_nearPlane; }
    float getFarPlane() const { return m_farPlane; }
    float getAspectRatio() const { return m_aspectRatio; }

    // Matrix getters (non-const versions update if dirty)
    const glm::mat4& getViewMatrix();
    const glm::mat4& getProjectionMatrix();
    glm::mat4 getViewProjectionMatrix();

    // Const matrix getters (return cached matrices, won't update if dirty)
    const glm::mat4& getViewMatrix() const { return m_viewMatrix; }
    const glm::mat4& getProjectionMatrix() const { return m_projMatrix; }
    glm::mat4 getViewProjectionMatrix() const { return m_projMatrix * m_viewMatrix; }

    // Look at a target from current position
    void lookAt(const glm::vec3& target);
    void lookAt(const glm::vec3& position, const glm::vec3& target, const glm::vec3& up = glm::vec3(0, 1, 0));

    // Interactive controls
    void orbit(float yawDelta, float pitchDelta);    // Orbit around target
    void pan(float dx, float dy);                     // Pan in screen space
    void zoom(float delta);                           // Move toward/away from target
    void dolly(float delta);                          // Move forward/backward

    // Camera direction vectors
    glm::vec3 getForward() const;
    glm::vec3 getRight() const;
    glm::vec3 getUpVector() const;

    // Distance to target
    float getDistance() const { return glm::length(m_target - m_position); }
    void setDistance(float distance);

private:
    glm::vec3 m_position{0.0f, 0.0f, 3.0f};
    glm::vec3 m_target{0.0f, 0.0f, 0.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};

    float m_fov{glm::radians(60.0f)};
    float m_nearPlane{0.1f};
    float m_farPlane{1000.0f};
    float m_aspectRatio{16.0f / 9.0f};

    glm::mat4 m_viewMatrix{1.0f};
    glm::mat4 m_projMatrix{1.0f};

    bool m_dirty{true};
    bool m_projDirty{true};

    void updateViewMatrix();
    void updateProjectionMatrix();
};

// Uniform buffer structure for 3D rendering
struct CameraUniforms {
    glm::mat4 viewProjection;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec4 cameraPosition;  // xyz = position, w = unused
};

// Model transform uniform buffer
struct ModelUniforms {
    glm::mat4 model;
    glm::mat4 normalMatrix;  // transpose(inverse(model)) for normals
};

} // namespace vivid
