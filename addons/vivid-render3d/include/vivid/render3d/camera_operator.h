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

    /// Set projection mode (Perspective or Orthographic)
    CameraOperator& projectionMode(ProjectionMode mode) {
        if (m_projectionMode != mode) {
            m_projectionMode = mode;
            markDirty();
        }
        return *this;
    }

    /// Set to perspective projection (default)
    CameraOperator& perspective() {
        return projectionMode(ProjectionMode::Perspective);
    }

    /// Set to orthographic projection
    CameraOperator& orthographic() {
        return projectionMode(ProjectionMode::Orthographic);
    }

    /// Set vertical field of view in degrees (perspective mode)
    CameraOperator& fov(float degrees) {
        if (m_fov != degrees) {
            m_fov = degrees;
            markDirty();
        }
        return *this;
    }

    /// Set orthographic size (vertical extent in world units)
    CameraOperator& orthoSize(float size) {
        if (m_orthoSize != size) {
            m_orthoSize = size;
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

    /// Connect ortho size to an operator output (orthographic mode)
    CameraOperator& orthoSizeInput(Operator* op) {
        setInput(4, op);
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
        float orthoSz = m_orthoSize;

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
        if (auto* input = getInput(4)) {
            orthoSz = input->outputValue();
        }

        // Update camera
        if (m_orbitMode) {
            m_camera.orbit(m_target, dist, azim, elev);
        } else {
            m_camera.lookAt(m_position, m_target);
        }

        m_camera.fov(fov)
            .nearPlane(m_near)
            .farPlane(m_far)
            .projectionMode(m_projectionMode)
            .orthoSize(orthoSz);

        didCook();
    }

    void cleanup() override {}

    std::string name() const override { return "Camera"; }

    std::vector<ParamDecl> params() override {
        std::vector<ParamDecl> result;

        if (m_orbitMode) {
            result.push_back({"center", ParamType::Vec3, -100.0f, 100.0f,
                 {m_target.x, m_target.y, m_target.z, 0}});
            result.push_back({"distance", ParamType::Float, 0.1f, 100.0f, {m_distance, 0, 0, 0}});
            result.push_back({"azimuth", ParamType::Float, -6.28f, 6.28f, {m_azimuth, 0, 0, 0}});
            result.push_back({"elevation", ParamType::Float, -1.57f, 1.57f, {m_elevation, 0, 0, 0}});
        } else {
            result.push_back({"position", ParamType::Vec3, -100.0f, 100.0f,
                 {m_position.x, m_position.y, m_position.z, 0}});
            result.push_back({"target", ParamType::Vec3, -100.0f, 100.0f,
                 {m_target.x, m_target.y, m_target.z, 0}});
        }

        // Projection mode: 0 = Perspective, 1 = Orthographic
        float projMode = (m_projectionMode == ProjectionMode::Orthographic) ? 1.0f : 0.0f;
        result.push_back({"projectionMode", ParamType::Int, 0.0f, 1.0f, {projMode, 0, 0, 0}});

        if (m_projectionMode == ProjectionMode::Perspective) {
            result.push_back({"fov", ParamType::Float, 1.0f, 179.0f, {m_fov, 0, 0, 0}});
        } else {
            result.push_back({"orthoSize", ParamType::Float, 0.1f, 1000.0f, {m_orthoSize, 0, 0, 0}});
        }

        result.push_back({"near", ParamType::Float, 0.001f, 10.0f, {m_near, 0, 0, 0}});
        result.push_back({"far", ParamType::Float, 1.0f, 10000.0f, {m_far, 0, 0, 0}});

        return result;
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "center" || name == "target") {
            out[0] = m_target.x; out[1] = m_target.y; out[2] = m_target.z; out[3] = 0;
            return true;
        }
        if (name == "position") {
            out[0] = m_position.x; out[1] = m_position.y; out[2] = m_position.z; out[3] = 0;
            return true;
        }
        if (name == "distance") { out[0] = m_distance; return true; }
        if (name == "azimuth") { out[0] = m_azimuth; return true; }
        if (name == "elevation") { out[0] = m_elevation; return true; }
        if (name == "fov") { out[0] = m_fov; return true; }
        if (name == "orthoSize") { out[0] = m_orthoSize; return true; }
        if (name == "projectionMode") {
            out[0] = (m_projectionMode == ProjectionMode::Orthographic) ? 1.0f : 0.0f;
            return true;
        }
        if (name == "near") { out[0] = m_near; return true; }
        if (name == "far") { out[0] = m_far; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "center") {
            m_target = glm::vec3(value[0], value[1], value[2]);
            markDirty();
            return true;
        }
        if (name == "target") {
            m_target = glm::vec3(value[0], value[1], value[2]);
            markDirty();
            return true;
        }
        if (name == "position") {
            m_position = glm::vec3(value[0], value[1], value[2]);
            markDirty();
            return true;
        }
        if (name == "distance") { m_distance = value[0]; markDirty(); return true; }
        if (name == "azimuth") { m_azimuth = value[0]; markDirty(); return true; }
        if (name == "elevation") { m_elevation = value[0]; markDirty(); return true; }
        if (name == "fov") { m_fov = value[0]; markDirty(); return true; }
        if (name == "orthoSize") { m_orthoSize = value[0]; markDirty(); return true; }
        if (name == "projectionMode") {
            m_projectionMode = (value[0] > 0.5f) ? ProjectionMode::Orthographic : ProjectionMode::Perspective;
            markDirty();
            return true;
        }
        if (name == "near") { m_near = value[0]; markDirty(); return true; }
        if (name == "far") { m_far = value[0]; markDirty(); return true; }
        return false;
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
    ProjectionMode m_projectionMode = ProjectionMode::Perspective;
    float m_fov = 45.0f;
    float m_orthoSize = 10.0f;
    float m_near = 0.1f;
    float m_far = 100.0f;
};

} // namespace vivid::render3d
