#pragma once

/**
 * @file light_operators.h
 * @brief Light operators for the node-based workflow
 *
 * Provides operator implementations for lighting:
 * - DirectionalLight (sun-like, infinite distance)
 * - PointLight (omnidirectional, with falloff)
 * - SpotLight (cone-shaped, with falloff)
 *
 * Light operators can be connected to Render3D via lightInput().
 */

#include <vivid/operator.h>
#include <vivid/context.h>
#include <glm/glm.hpp>

namespace vivid::render3d {

/**
 * @brief Light type enumeration
 */
enum class LightType {
    Directional,  ///< Parallel rays, like sunlight
    Point,        ///< Omnidirectional, like a light bulb
    Spot          ///< Cone-shaped, like a flashlight
};

/**
 * @brief Light data structure
 *
 * Contains all parameters needed to describe a light source.
 * Render3D reads this from LightOperator::outputLight().
 */
struct LightData {
    LightType type = LightType::Directional;
    glm::vec3 direction = glm::vec3(1, 2, 1);   ///< Direction (Directional/Spot)
    glm::vec3 position = glm::vec3(0, 5, 0);    ///< Position (Point/Spot)
    glm::vec3 color = glm::vec3(1, 1, 1);       ///< Light color
    float intensity = 1.0f;                      ///< Light intensity multiplier
    float range = 10.0f;                         ///< Falloff distance (Point/Spot)
    float spotAngle = 45.0f;                     ///< Outer cone angle in degrees (Spot)
    float spotBlend = 0.1f;                      ///< Inner/outer cone blend (Spot)
};

/**
 * @brief Base class for light operators
 *
 * Provides common interface for all light types. Derived classes
 * configure the LightData and can be connected to Render3D.
 */
class LightOperator : public Operator {
public:
    /**
     * @brief Get the light data
     * @return Reference to the light configuration
     */
    virtual const LightData& outputLight() const { return m_light; }

    /**
     * @brief Get output kind (Light)
     */
    OutputKind outputKind() const override { return OutputKind::Light; }

protected:
    LightData m_light;
};

// =============================================================================
// DirectionalLight
// =============================================================================

/**
 * @brief Directional light operator (sun-like)
 *
 * Creates parallel light rays from a specified direction.
 * Has no position or falloff - illuminates everything equally.
 *
 * @par Example
 * @code
 * auto& sun = chain.add<DirectionalLight>("sun")
 *     .direction(1, 2, 1)
 *     .color(1.0f, 0.95f, 0.9f)
 *     .intensity(1.5f);
 *
 * auto& render = chain.add<Render3D>("render")
 *     .input(&scene)
 *     .lightInput(&sun);
 * @endcode
 */
class DirectionalLight : public LightOperator {
public:
    DirectionalLight() {
        m_light.type = LightType::Directional;
    }

    /// Set light direction (will be normalized)
    DirectionalLight& direction(float x, float y, float z) {
        m_light.direction = glm::normalize(glm::vec3(x, y, z));
        return *this;
    }

    /// Set light direction from vector
    DirectionalLight& direction(const glm::vec3& dir) {
        m_light.direction = glm::normalize(dir);
        return *this;
    }

    /// Set light color (RGB, 0-1)
    DirectionalLight& color(float r, float g, float b) {
        m_light.color = glm::vec3(r, g, b);
        return *this;
    }

    /// Set light color from vector
    DirectionalLight& color(const glm::vec3& c) {
        m_light.color = c;
        return *this;
    }

    /// Set intensity multiplier
    DirectionalLight& intensity(float i) {
        m_light.intensity = i;
        return *this;
    }

    /// Connect intensity to another operator's output value
    DirectionalLight& intensityInput(Operator* op) {
        setInput(0, op);
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        // Read animated intensity if connected
        if (auto* input = getInput(0)) {
            m_light.intensity = input->outputValue();
        }
    }

    void cleanup() override {}

    std::string name() const override { return "DirectionalLight"; }

    std::vector<ParamDecl> params() override {
        return {
            {"direction", ParamType::Vec3, -1.0f, 1.0f,
             {m_light.direction.x, m_light.direction.y, m_light.direction.z, 0}},
            {"color", ParamType::Color, 0.0f, 1.0f,
             {m_light.color.r, m_light.color.g, m_light.color.b, 1.0f}},
            {"intensity", ParamType::Float, 0.0f, 10.0f, {m_light.intensity, 0, 0, 0}}
        };
    }
};

// =============================================================================
// PointLight
// =============================================================================

/**
 * @brief Point light operator (omnidirectional)
 *
 * Creates light that radiates equally in all directions from a point.
 * Has position and range for distance falloff.
 *
 * @par Example
 * @code
 * auto& bulb = chain.add<PointLight>("bulb")
 *     .position(0, 3, 0)
 *     .color(1.0f, 0.9f, 0.8f)
 *     .intensity(2.0f)
 *     .range(15.0f);
 * @endcode
 */
class PointLight : public LightOperator {
public:
    PointLight() {
        m_light.type = LightType::Point;
    }

    /// Set light position
    PointLight& position(float x, float y, float z) {
        m_light.position = glm::vec3(x, y, z);
        return *this;
    }

    /// Set light position from vector
    PointLight& position(const glm::vec3& pos) {
        m_light.position = pos;
        return *this;
    }

    /// Set light color (RGB, 0-1)
    PointLight& color(float r, float g, float b) {
        m_light.color = glm::vec3(r, g, b);
        return *this;
    }

    /// Set light color from vector
    PointLight& color(const glm::vec3& c) {
        m_light.color = c;
        return *this;
    }

    /// Set intensity multiplier
    PointLight& intensity(float i) {
        m_light.intensity = i;
        return *this;
    }

    /// Set falloff range
    PointLight& range(float r) {
        m_light.range = r;
        return *this;
    }

    /// Connect intensity to another operator's output value
    PointLight& intensityInput(Operator* op) {
        setInput(0, op);
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        if (auto* input = getInput(0)) {
            m_light.intensity = input->outputValue();
        }
    }

    void cleanup() override {}

    std::string name() const override { return "PointLight"; }

    std::vector<ParamDecl> params() override {
        return {
            {"position", ParamType::Vec3, -100.0f, 100.0f,
             {m_light.position.x, m_light.position.y, m_light.position.z, 0}},
            {"color", ParamType::Color, 0.0f, 1.0f,
             {m_light.color.r, m_light.color.g, m_light.color.b, 1.0f}},
            {"intensity", ParamType::Float, 0.0f, 10.0f, {m_light.intensity, 0, 0, 0}},
            {"range", ParamType::Float, 0.1f, 100.0f, {m_light.range, 0, 0, 0}}
        };
    }
};

// =============================================================================
// SpotLight
// =============================================================================

/**
 * @brief Spot light operator (cone-shaped)
 *
 * Creates a cone of light from a position in a direction.
 * Has position, direction, range, and cone angle parameters.
 *
 * @par Example
 * @code
 * auto& spot = chain.add<SpotLight>("spotlight")
 *     .position(0, 5, 0)
 *     .direction(0, -1, 0)
 *     .spotAngle(30.0f)
 *     .intensity(3.0f);
 * @endcode
 */
class SpotLight : public LightOperator {
public:
    SpotLight() {
        m_light.type = LightType::Spot;
        m_light.direction = glm::vec3(0, -1, 0);  // Default pointing down
    }

    /// Set light position
    SpotLight& position(float x, float y, float z) {
        m_light.position = glm::vec3(x, y, z);
        return *this;
    }

    /// Set light position from vector
    SpotLight& position(const glm::vec3& pos) {
        m_light.position = pos;
        return *this;
    }

    /// Set light direction (will be normalized)
    SpotLight& direction(float x, float y, float z) {
        m_light.direction = glm::normalize(glm::vec3(x, y, z));
        return *this;
    }

    /// Set light direction from vector
    SpotLight& direction(const glm::vec3& dir) {
        m_light.direction = glm::normalize(dir);
        return *this;
    }

    /// Set light color (RGB, 0-1)
    SpotLight& color(float r, float g, float b) {
        m_light.color = glm::vec3(r, g, b);
        return *this;
    }

    /// Set light color from vector
    SpotLight& color(const glm::vec3& c) {
        m_light.color = c;
        return *this;
    }

    /// Set intensity multiplier
    SpotLight& intensity(float i) {
        m_light.intensity = i;
        return *this;
    }

    /// Set falloff range
    SpotLight& range(float r) {
        m_light.range = r;
        return *this;
    }

    /// Set outer cone angle in degrees
    SpotLight& spotAngle(float degrees) {
        m_light.spotAngle = degrees;
        return *this;
    }

    /// Set inner/outer cone blend factor (0 = hard edge, 1 = soft)
    SpotLight& spotBlend(float blend) {
        m_light.spotBlend = blend;
        return *this;
    }

    /// Connect intensity to another operator's output value
    SpotLight& intensityInput(Operator* op) {
        setInput(0, op);
        return *this;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        if (auto* input = getInput(0)) {
            m_light.intensity = input->outputValue();
        }
    }

    void cleanup() override {}

    std::string name() const override { return "SpotLight"; }

    std::vector<ParamDecl> params() override {
        return {
            {"position", ParamType::Vec3, -100.0f, 100.0f,
             {m_light.position.x, m_light.position.y, m_light.position.z, 0}},
            {"direction", ParamType::Vec3, -1.0f, 1.0f,
             {m_light.direction.x, m_light.direction.y, m_light.direction.z, 0}},
            {"color", ParamType::Color, 0.0f, 1.0f,
             {m_light.color.r, m_light.color.g, m_light.color.b, 1.0f}},
            {"intensity", ParamType::Float, 0.0f, 10.0f, {m_light.intensity, 0, 0, 0}},
            {"range", ParamType::Float, 0.1f, 100.0f, {m_light.range, 0, 0, 0}},
            {"spotAngle", ParamType::Float, 1.0f, 180.0f, {m_light.spotAngle, 0, 0, 0}}
        };
    }
};

} // namespace vivid::render3d
