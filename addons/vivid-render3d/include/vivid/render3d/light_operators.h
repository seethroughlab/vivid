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
#include <vivid/param_registry.h>
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

    // Shadow parameters
    bool castShadow = false;                     ///< Whether this light casts shadows
    float shadowBias = 0.001f;                   ///< Depth bias to prevent shadow acne

    // Debug visualization
    bool drawDebug = false;                      ///< Draw wireframe visualization of light
};

/**
 * @brief Base class for light operators
 *
 * Provides common interface for all light types. Derived classes
 * configure the LightData and can be connected to Render3D.
 */
class LightOperator : public Operator, public ParamRegistry {
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
    Param<float> intensity{"intensity", 1.0f, 0.0f, 10.0f};  ///< Light intensity multiplier

    DirectionalLight() {
        m_light.type = LightType::Directional;
        registerParam(intensity);
    }

    /// Set light direction (will be normalized)
    void direction(float x, float y, float z) {
        glm::vec3 newDir = glm::normalize(glm::vec3(x, y, z));
        if (m_light.direction != newDir) {
            m_light.direction = newDir;
            markDirty();
        }
    }

    /// Set light direction from vector
    void direction(const glm::vec3& dir) {
        glm::vec3 newDir = glm::normalize(dir);
        if (m_light.direction != newDir) {
            m_light.direction = newDir;
            markDirty();
        }
    }

    /// Set light color (RGB, 0-1)
    void color(float r, float g, float b) {
        glm::vec3 newColor(r, g, b);
        if (m_light.color != newColor) {
            m_light.color = newColor;
            markDirty();
        }
    }

    /// Set light color from vector
    void color(const glm::vec3& c) {
        if (m_light.color != c) {
            m_light.color = c;
            markDirty();
        }
    }

    /// Enable/disable shadow casting for this light
    void castShadow(bool enabled) {
        if (m_light.castShadow != enabled) {
            m_light.castShadow = enabled;
            markDirty();
        }
    }

    /// Set shadow depth bias (0.0001 - 0.01, default 0.001)
    /// Higher values reduce shadow acne but can cause peter panning
    void shadowBias(float bias) {
        if (m_light.shadowBias != bias) {
            m_light.shadowBias = bias;
            markDirty();
        }
    }

    /// Enable/disable debug wireframe visualization
    void drawDebug(bool enabled) {
        m_light.drawDebug = enabled;
    }

    /// Connect intensity to another operator's output value
    void intensityInput(Operator* op) {
        setInput(0, op);
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        // Sync param to LightData
        m_light.intensity = static_cast<float>(intensity);

        // Read animated intensity if connected (overrides param)
        if (auto* input = getInput(0)) {
            m_light.intensity = input->outputValue();
        }

        // Notify downstream operators (like Render3D) when dirty
        if (needsCook()) {
            didCook();
        }
    }

    void cleanup() override {}

    std::string name() const override { return "DirectionalLight"; }
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
    Param<float> intensity{"intensity", 1.0f, 0.0f, 10.0f};  ///< Light intensity multiplier
    Param<float> range{"range", 10.0f, 0.1f, 100.0f};        ///< Falloff distance

    PointLight() {
        m_light.type = LightType::Point;
        registerParam(intensity);
        registerParam(range);
    }

    /// Set light position
    void position(float x, float y, float z) {
        glm::vec3 newPos(x, y, z);
        if (m_light.position != newPos) {
            m_light.position = newPos;
            markDirty();
        }
    }

    /// Set light position from vector
    void position(const glm::vec3& pos) {
        if (m_light.position != pos) {
            m_light.position = pos;
            markDirty();
        }
    }

    /// Set light color (RGB, 0-1)
    void color(float r, float g, float b) {
        glm::vec3 newColor(r, g, b);
        if (m_light.color != newColor) {
            m_light.color = newColor;
            markDirty();
        }
    }

    /// Set light color from vector
    void color(const glm::vec3& c) {
        if (m_light.color != c) {
            m_light.color = c;
            markDirty();
        }
    }

    /// Connect intensity to another operator's output value
    void intensityInput(Operator* op) {
        setInput(0, op);
    }

    /// Enable/disable shadow casting for this light
    /// Note: Point light shadows use cube maps (6 shadow passes)
    void castShadow(bool enabled) {
        if (m_light.castShadow != enabled) {
            m_light.castShadow = enabled;
            markDirty();
        }
    }

    /// Set shadow depth bias (0.0001 - 0.01, default 0.001)
    /// Higher values reduce shadow acne but can cause peter panning
    void shadowBias(float bias) {
        if (m_light.shadowBias != bias) {
            m_light.shadowBias = bias;
            markDirty();
        }
    }

    /// Enable/disable debug wireframe visualization
    void drawDebug(bool enabled) {
        m_light.drawDebug = enabled;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        // Sync params to LightData
        m_light.intensity = static_cast<float>(intensity);
        m_light.range = static_cast<float>(range);

        // Read animated intensity if connected (overrides param)
        if (auto* input = getInput(0)) {
            m_light.intensity = input->outputValue();
        }

        // Notify downstream operators when dirty
        if (needsCook()) {
            didCook();
        }
    }

    void cleanup() override {}

    std::string name() const override { return "PointLight"; }
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
    Param<float> intensity{"intensity", 1.0f, 0.0f, 10.0f};    ///< Light intensity multiplier
    Param<float> range{"range", 10.0f, 0.1f, 100.0f};          ///< Falloff distance
    Param<float> spotAngle{"spotAngle", 45.0f, 1.0f, 180.0f};  ///< Outer cone angle in degrees
    Param<float> spotBlend{"spotBlend", 0.1f, 0.0f, 1.0f};     ///< Inner/outer cone blend

    SpotLight() {
        m_light.type = LightType::Spot;
        m_light.direction = glm::vec3(0, -1, 0);  // Default pointing down
        registerParam(intensity);
        registerParam(range);
        registerParam(spotAngle);
        registerParam(spotBlend);
    }

    /// Set light position
    void position(float x, float y, float z) {
        glm::vec3 newPos(x, y, z);
        if (m_light.position != newPos) {
            m_light.position = newPos;
            markDirty();
        }
    }

    /// Set light position from vector
    void position(const glm::vec3& pos) {
        if (m_light.position != pos) {
            m_light.position = pos;
            markDirty();
        }
    }

    /// Set light direction (will be normalized)
    void direction(float x, float y, float z) {
        glm::vec3 newDir = glm::normalize(glm::vec3(x, y, z));
        if (m_light.direction != newDir) {
            m_light.direction = newDir;
            markDirty();
        }
    }

    /// Set light direction from vector
    void direction(const glm::vec3& dir) {
        glm::vec3 newDir = glm::normalize(dir);
        if (m_light.direction != newDir) {
            m_light.direction = newDir;
            markDirty();
        }
    }

    /// Set light color (RGB, 0-1)
    void color(float r, float g, float b) {
        glm::vec3 newColor(r, g, b);
        if (m_light.color != newColor) {
            m_light.color = newColor;
            markDirty();
        }
    }

    /// Set light color from vector
    void color(const glm::vec3& c) {
        if (m_light.color != c) {
            m_light.color = c;
            markDirty();
        }
    }

    /// Connect intensity to another operator's output value
    void intensityInput(Operator* op) {
        setInput(0, op);
    }

    /// Enable/disable shadow casting for this light
    void castShadow(bool enabled) {
        if (m_light.castShadow != enabled) {
            m_light.castShadow = enabled;
            markDirty();
        }
    }

    /// Set shadow depth bias (0.0001 - 0.01, default 0.001)
    /// Higher values reduce shadow acne but can cause peter panning
    void shadowBias(float bias) {
        if (m_light.shadowBias != bias) {
            m_light.shadowBias = bias;
            markDirty();
        }
    }

    /// Enable/disable debug wireframe visualization
    void drawDebug(bool enabled) {
        m_light.drawDebug = enabled;
    }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        // Sync params to LightData
        m_light.intensity = static_cast<float>(intensity);
        m_light.range = static_cast<float>(range);
        m_light.spotAngle = static_cast<float>(spotAngle);
        m_light.spotBlend = static_cast<float>(spotBlend);

        // Read animated intensity if connected (overrides param)
        if (auto* input = getInput(0)) {
            m_light.intensity = input->outputValue();
        }

        // Notify downstream operators when dirty
        if (needsCook()) {
            didCook();
        }
    }

    void cleanup() override {}

    std::string name() const override { return "SpotLight"; }
};

} // namespace vivid::render3d
