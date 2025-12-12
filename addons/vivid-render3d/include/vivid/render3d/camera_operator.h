#pragma once

/**
 * @file camera_operator.h
 * @brief Camera operator for the node-based workflow
 *
 * CameraOperator wraps Camera3D and makes it a chainable node with
 * animatable inputs for FOV, distance, azimuth, and elevation.
 *
 * Can be connected to Render3D via cameraInput().
 */

#include <vivid/operator.h>
#include <vivid/context.h>
#include <vivid/render3d/camera.h>

namespace vivid::render3d {

/**
 * @brief Camera operator for the node-based workflow
 *
 * Wraps Camera3D and exposes it as a chainable operator with animatable
 * inputs. Supports both direct position/target and orbit camera modes.
 *
 * @par Example - Static Camera
 * @code
 * auto& camera = chain.add<CameraOperator>("camera")
 *     .position(5, 3, 5)
 *     .target(0, 0, 0)
 *     .fov(60.0f);
 *
 * auto& render = chain.add<Render3D>("render")
 *     .input(&scene)
 *     .cameraInput(&camera);
 * @endcode
 *
 * @par Example - Animated Orbit Camera
 * @code
 * auto& time = chain.add<Time>("time");
 * auto& camera = chain.add<CameraOperator>("camera")
 *     .orbitCenter(0, 0, 0)
 *     .distance(8.0f)
 *     .elevation(0.4f)
 *     .azimuthInput(&time);  // Animate rotation
 * @endcode
 */
class CameraOperator : public Operator {
public:
    CameraOperator() = default;

    // -------------------------------------------------------------------------
    /// @name Position and Target
    /// @{

    /// Set camera position (disables orbit mode)
    CameraOperator& position(float x, float y, float z) {
        glm::vec3 newPos(x, y, z);
        if (m_position != newPos || m_orbitMode) {
            m_position = newPos;
            m_orbitMode = false;
            markDirty();
        }
        return *this;
    }

    /// Set camera position from vector
    CameraOperator& position(const glm::vec3& pos) {
        if (m_position != pos || m_orbitMode) {
            m_position = pos;
            m_orbitMode = false;
            markDirty();
        }
        return *this;
    }

    /// Set look-at target
    CameraOperator& target(float x, float y, float z) {
        glm::vec3 newTarget(x, y, z);
        if (m_target != newTarget) {
            m_target = newTarget;
            markDirty();
        }
        return *this;
    }

    /// Set look-at target from vector
    CameraOperator& target(const glm::vec3& t) {
        if (m_target != t) {
            m_target = t;
            markDirty();
        }
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Orbit Mode
    /// @{

    /// Set orbit center point (enables orbit mode)
    CameraOperator& orbitCenter(float x, float y, float z) {
        glm::vec3 newTarget(x, y, z);
        if (m_target != newTarget || !m_orbitMode) {
            m_target = newTarget;
            m_orbitMode = true;
            markDirty();
        }
        return *this;
    }

    /// Set orbit center from vector
    CameraOperator& orbitCenter(const glm::vec3& center) {
        if (m_target != center || !m_orbitMode) {
            m_target = center;
            m_orbitMode = true;
            markDirty();
        }
        return *this;
    }

    /// Set orbit distance
    CameraOperator& distance(float d) {
        if (m_distance != d || !m_orbitMode) {
            m_distance = d;
            m_orbitMode = true;
            markDirty();
        }
        return *this;
    }

    /// Set orbit azimuth angle in radians
    CameraOperator& azimuth(float radians) {
        if (m_azimuth != radians || !m_orbitMode) {
            m_azimuth = radians;
            m_orbitMode = true;
            markDirty();
        }
        return *this;
    }

    /// Set orbit elevation angle in radians
    CameraOperator& elevation(float radians) {
        if (m_elevation != radians || !m_orbitMode) {
            m_elevation = radians;
            m_orbitMode = true;
            markDirty();
        }
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Projection
    /// @{

    /// Set vertical field of view in degrees
    CameraOperator& fov(float degrees) {
        if (m_fov != degrees) {
            m_fov = degrees;
            markDirty();
        }
        return *this;
    }

    /// Set near clip plane
    CameraOperator& nearPlane(float n) {
        if (m_near != n) {
            m_near = n;
            markDirty();
        }
        return *this;
    }

    /// Set far clip plane
    CameraOperator& farPlane(float f) {
        if (m_far != f) {
            m_far = f;
            markDirty();
        }
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Animated Inputs
    /// @{

    /// Connect FOV to an operator output (degrees)
    CameraOperator& fovInput(Operator* op) {
        setInput(0, op);
        return *this;
    }

    /// Connect distance to an operator output (orbit mode)
    CameraOperator& distanceInput(Operator* op) {
        setInput(1, op);
        m_orbitMode = true;
        return *this;
    }

    /// Connect azimuth to an operator output (radians, orbit mode)
    CameraOperator& azimuthInput(Operator* op) {
        setInput(2, op);
        m_orbitMode = true;
        return *this;
    }

    /// Connect elevation to an operator output (radians, orbit mode)
    CameraOperator& elevationInput(Operator* op) {
        setInput(3, op);
        m_orbitMode = true;
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Output
    /// @{

    /// Get the configured camera
    const Camera3D& outputCamera() const { return m_camera; }

    /// Get output kind (Camera)
    OutputKind outputKind() const override { return OutputKind::Camera; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Lifecycle
    /// @{

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        if (!needsCook()) return;

        // Read animated inputs
        float fov = m_fov;
        float dist = m_distance;
        float azim = m_azimuth;
        float elev = m_elevation;

        if (auto* input = getInput(0)) {
            fov = input->outputValue();
        }
        if (auto* input = getInput(1)) {
            dist = input->outputValue();
        }
        if (auto* input = getInput(2)) {
            azim = input->outputValue();
        }
        if (auto* input = getInput(3)) {
            elev = input->outputValue();
        }

        // Update camera
        if (m_orbitMode) {
            m_camera.orbit(m_target, dist, azim, elev);
        } else {
            m_camera.lookAt(m_position, m_target);
        }

        m_camera.fov(fov).nearPlane(m_near).farPlane(m_far);

        didCook();
    }

    void cleanup() override {}

    std::string name() const override { return "Camera"; }

    std::vector<ParamDecl> params() override {
        if (m_orbitMode) {
            return {
                {"center", ParamType::Vec3, -100.0f, 100.0f,
                 {m_target.x, m_target.y, m_target.z, 0}},
                {"distance", ParamType::Float, 0.1f, 100.0f, {m_distance, 0, 0, 0}},
                {"azimuth", ParamType::Float, -6.28f, 6.28f, {m_azimuth, 0, 0, 0}},
                {"elevation", ParamType::Float, -1.57f, 1.57f, {m_elevation, 0, 0, 0}},
                {"fov", ParamType::Float, 1.0f, 179.0f, {m_fov, 0, 0, 0}}
            };
        } else {
            return {
                {"position", ParamType::Vec3, -100.0f, 100.0f,
                 {m_position.x, m_position.y, m_position.z, 0}},
                {"target", ParamType::Vec3, -100.0f, 100.0f,
                 {m_target.x, m_target.y, m_target.z, 0}},
                {"fov", ParamType::Float, 1.0f, 179.0f, {m_fov, 0, 0, 0}}
            };
        }
    }

    /// @}

private:
    Camera3D m_camera;

    // Position/target mode
    glm::vec3 m_position = glm::vec3(0, 0, 5);
    glm::vec3 m_target = glm::vec3(0, 0, 0);

    // Orbit mode
    bool m_orbitMode = false;
    float m_distance = 5.0f;
    float m_azimuth = 0.0f;
    float m_elevation = 0.3f;

    // Projection
    float m_fov = 45.0f;
    float m_near = 0.1f;
    float m_far = 100.0f;
};

} // namespace vivid::render3d
