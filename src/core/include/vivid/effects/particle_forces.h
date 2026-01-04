#pragma once

/**
 * @file particle_forces.h
 * @brief Base class for modular particle forces
 *
 * Forces can operate on both CPU and GPU. Each force provides:
 * - CPU computation for prototyping and small particle counts
 * - WGSL code generation for GPU compute shaders
 * - Uniform data for GPU pipeline
 */

#include <vivid/param.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

namespace vivid::effects {

// Forward declaration
struct Particle;

/**
 * @brief Unique identifier for force types (used for GPU shader selection)
 */
enum class ForceType {
    CurlNoise,
    Gravity,
    Drag,
    PointAttractor,
    Vortex,
    Wind,
    VelocityField,
    Turbulence,
    Custom  // CPU-only custom forces
};

/**
 * @brief Base class for particle forces
 *
 * Forces can operate on both CPU and GPU. Each force provides:
 * - CPU computation for prototyping and small particle counts
 * - WGSL code generation for GPU compute shaders
 * - Uniform data for GPU pipeline
 */
class ParticleForce {
public:
    virtual ~ParticleForce() = default;

    /// @brief Unique type identifier
    virtual ForceType type() const = 0;

    /// @brief Human-readable name
    virtual std::string name() const = 0;

    /// @brief Whether this force is enabled
    bool enabled = true;

    // =========================================================================
    /// @name CPU Simulation
    /// @{

    /**
     * @brief Compute force contribution for a single particle (CPU mode)
     * @param p The particle to compute force for
     * @param time Current simulation time
     * @param dt Delta time
     * @return Force vector to add to velocity (already scaled by dt if needed)
     */
    virtual glm::vec3 compute(const Particle& p, float time, float dt) = 0;

    /// @}
    // =========================================================================
    /// @name GPU Shader Generation
    /// @{

    /**
     * @brief Get WGSL uniform fields for this force
     * @return WGSL field definitions (without struct wrapper)
     *
     * Example return: "  curlStrength: f32,\n  curlScale: f32,\n"
     */
    virtual std::string wgslUniformFields() const { return ""; }

    /**
     * @brief Get WGSL helper functions required by this force
     * @return WGSL function definitions
     */
    virtual std::string wgslHelperFunctions() const { return ""; }

    /**
     * @brief Get WGSL force computation code
     * @param uniformsVar Name of uniforms variable (e.g., "u")
     * @return WGSL code that modifies 'vel' variable
     *
     * Code should assume these variables exist:
     * - pos: vec3f (particle position)
     * - vel: vec3f (particle velocity, modify this)
     * - time: f32 (simulation time)
     * - dt: f32 (delta time)
     * - is3D: bool (2D vs 3D mode)
     * - seed: f32 (per-particle random seed)
     */
    virtual std::string wgslComputeCode(const std::string& uniformsVar) const { return ""; }

    /**
     * @brief Get size of uniform data in bytes
     * @return Size in bytes (must be 4-byte aligned)
     */
    virtual size_t uniformSize() const { return 0; }

    /**
     * @brief Write uniform data to buffer
     * @param dest Destination buffer (at least uniformSize() bytes)
     */
    virtual void writeUniforms(void* dest) const {}

    /// @}
    // =========================================================================
    /// @name Parameters
    /// @{

    /**
     * @brief Get parameter declarations for UI/introspection
     * @return Vector of ParamDecl
     */
    virtual std::vector<ParamDecl> params() const { return {}; }

    /**
     * @brief Get parameter value
     * @param name Parameter name
     * @param out Output array
     * @return true if parameter found
     */
    virtual bool getParam(const std::string& name, float out[4]) const { return false; }

    /**
     * @brief Set parameter value
     * @param name Parameter name
     * @param value Value array
     * @return true if parameter was set
     */
    virtual bool setParam(const std::string& name, const float value[4]) { return false; }

    /// @}
};

// Convenience alias
using ParticleForcePtr = std::unique_ptr<ParticleForce>;

} // namespace vivid::effects
