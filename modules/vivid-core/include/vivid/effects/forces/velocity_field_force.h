#pragma once

/**
 * @file velocity_field_force.h
 * @brief Velocity field / flowmap force (CPU-only)
 *
 * This force samples from a procedural or loaded velocity field.
 * Currently CPU-only due to texture binding complexity.
 */

#include <vivid/effects/particle_forces.h>

namespace vivid::effects {

/**
 * @brief Procedural velocity field modes
 */
enum class VelocityFieldMode {
    Circular,    ///< Circular flow around center
    Radial,      ///< Flow toward/away from center
    Spiral,      ///< Combined circular + radial
    Custom       ///< User-provided callback (future)
};

/**
 * @brief Velocity field force (CPU-only)
 *
 * Applies velocity from a procedural flow field. Useful for
 * creating structured motion patterns like circular flows,
 * radial expansion/contraction, and spirals.
 *
 * @par Parameters
 * - **mode**: Field type (Circular, Radial, Spiral)
 * - **center**: Field center position
 * - **strength**: Force strength
 * - **scale**: Field scale (smaller = tighter patterns)
 *
 * @note GPU support not implemented - requires texture binding
 */
class VelocityFieldForce : public ParticleForce {
public:
    VelocityFieldMode mode = VelocityFieldMode::Circular;
    Vec3Param center{"center", 0.5f, 0.5f, 0.0f, -10.0f, 10.0f};
    Param<float> strength{"strength", 0.0f, 0.0f, 10.0f};
    Param<float> scale{"scale", 1.0f, 0.1f, 10.0f};

    ForceType type() const override { return ForceType::VelocityField; }
    std::string name() const override { return "VelocityField"; }

    glm::vec3 compute(const Particle& p, float time, float dt) override {
        float s = static_cast<float>(strength);
        if (std::abs(s) < 0.001f) return glm::vec3(0.0f);

        glm::vec3 c(center.x(), center.y(), center.z());
        float sc = static_cast<float>(scale);

        // Vector from center to particle
        glm::vec3 toParticle = (p.position - c) * sc;
        float dist = glm::length(glm::vec2(toParticle.x, toParticle.y));

        if (dist < 0.001f) return glm::vec3(0.0f);

        glm::vec3 vel(0.0f);

        switch (mode) {
            case VelocityFieldMode::Circular: {
                // Perpendicular to radial (CCW rotation in XY)
                vel = glm::vec3(-toParticle.y, toParticle.x, 0.0f);
                vel = glm::normalize(vel);
                break;
            }
            case VelocityFieldMode::Radial: {
                // Toward/away from center
                vel = glm::normalize(toParticle);
                break;
            }
            case VelocityFieldMode::Spiral: {
                // Mix of circular and radial
                glm::vec3 circ = glm::normalize(glm::vec3(-toParticle.y, toParticle.x, 0.0f));
                glm::vec3 rad = glm::normalize(toParticle);
                vel = glm::normalize(circ * 0.7f + rad * 0.3f);
                break;
            }
            case VelocityFieldMode::Custom:
                // Future: callback or texture sampling
                break;
        }

        return vel * s * dt;
    }

    // GPU not supported for now - requires texture binding
    std::string wgslUniformFields() const override { return ""; }
    std::string wgslComputeCode(const std::string& u) const override { return ""; }
    size_t uniformSize() const override { return 0; }
    void writeUniforms(void* dest) const override {}

    std::vector<ParamDecl> params() const override {
        return { center.decl(), strength.decl(), scale.decl() };
    }

    bool getParam(const std::string& paramName, float out[4]) const override {
        if (paramName == "center") {
            out[0] = center.x(); out[1] = center.y(); out[2] = center.z();
            return true;
        }
        if (paramName == "strength") { out[0] = strength; return true; }
        if (paramName == "scale") { out[0] = scale; return true; }
        return false;
    }

    bool setParam(const std::string& paramName, const float value[4]) override {
        if (paramName == "center") { center.set(value[0], value[1], value[2]); return true; }
        if (paramName == "strength") { strength = value[0]; return true; }
        if (paramName == "scale") { scale = value[0]; return true; }
        return false;
    }
};

} // namespace vivid::effects
