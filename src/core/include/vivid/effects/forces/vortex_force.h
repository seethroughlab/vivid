#pragma once

/**
 * @file vortex_force.h
 * @brief Rotational vortex force around an axis
 */

#include <vivid/effects/particle_forces.h>

namespace vivid::effects {

/**
 * @brief Rotational vortex force
 *
 * Creates swirling motion around a central axis. Particles rotate
 * around the axis with force decreasing based on distance.
 *
 * @par Parameters
 * - **center**: Vortex center position
 * - **axis**: Rotation axis (normalized)
 * - **strength**: Rotation force (positive = CCW, negative = CW)
 * - **falloff**: Distance falloff exponent (0 = no falloff, 1 = linear, 2 = quadratic)
 */
class VortexForce : public ParticleForce {
public:
    Vec3Param center{"center", 0.5f, 0.5f, 0.0f, -10.0f, 10.0f};
    Vec3Param axis{"axis", 0.0f, 1.0f, 0.0f, -1.0f, 1.0f};
    Param<float> strength{"strength", 0.0f, -10.0f, 10.0f};
    Param<float> falloff{"falloff", 1.0f, 0.0f, 3.0f};
    Param<float> minDistance{"minDistance", 0.01f, 0.001f, 1.0f};

    ForceType type() const override { return ForceType::Vortex; }
    std::string name() const override { return "Vortex"; }

    glm::vec3 compute(const Particle& p, float time, float dt) override {
        float s = static_cast<float>(strength);
        if (std::abs(s) < 0.001f) return glm::vec3(0.0f);

        glm::vec3 c(center.x(), center.y(), center.z());
        glm::vec3 ax = glm::normalize(glm::vec3(axis.x(), axis.y(), axis.z()));
        float fall = static_cast<float>(falloff);
        float minDist = static_cast<float>(minDistance);

        // Vector from center to particle
        glm::vec3 toParticle = p.position - c;

        // Project onto plane perpendicular to axis
        float alongAxis = glm::dot(toParticle, ax);
        glm::vec3 radial = toParticle - ax * alongAxis;
        float dist = glm::length(radial);

        if (dist < minDist) return glm::vec3(0.0f);

        // Tangent direction (perpendicular to both axis and radial)
        glm::vec3 tangent = glm::cross(ax, glm::normalize(radial));

        // Apply falloff
        float forceMag = s;
        if (fall > 0.001f) {
            forceMag /= std::pow(dist, fall);
        }

        return tangent * forceMag * dt;
    }

    std::string wgslUniformFields() const override {
        return "  vortexCenterX: f32,\n"
               "  vortexCenterY: f32,\n"
               "  vortexCenterZ: f32,\n"
               "  vortexStrength: f32,\n"
               "  vortexAxisX: f32,\n"
               "  vortexAxisY: f32,\n"
               "  vortexAxisZ: f32,\n"
               "  vortexFalloff: f32,\n";
    }

    std::string wgslComputeCode(const std::string& u) const override {
        return "// Vortex\n"
               "if (abs(" + u + ".vortexStrength) > 0.001) {\n"
               "    let vCenter = vec3f(" + u + ".vortexCenterX, " + u + ".vortexCenterY, " + u + ".vortexCenterZ);\n"
               "    let vAxis = normalize(vec3f(" + u + ".vortexAxisX, " + u + ".vortexAxisY, " + u + ".vortexAxisZ));\n"
               "    let toP = pos - vCenter;\n"
               "    let alongAxis = dot(toP, vAxis);\n"
               "    let radial = toP - vAxis * alongAxis;\n"
               "    let dist = length(radial);\n"
               "    if (dist > 0.01) {\n"
               "        let tangent = cross(vAxis, normalize(radial));\n"
               "        var forceMag = " + u + ".vortexStrength;\n"
               "        if (" + u + ".vortexFalloff > 0.001) {\n"
               "            forceMag /= pow(dist, " + u + ".vortexFalloff);\n"
               "        }\n"
               "        vel += tangent * forceMag * dt;\n"
               "    }\n"
               "}\n";
    }

    size_t uniformSize() const override { return 32; }  // 8 floats

    void writeUniforms(void* dest) const override {
        float* f = static_cast<float*>(dest);
        f[0] = center.x();
        f[1] = center.y();
        f[2] = center.z();
        f[3] = static_cast<float>(strength);
        f[4] = axis.x();
        f[5] = axis.y();
        f[6] = axis.z();
        f[7] = static_cast<float>(falloff);
    }

    std::vector<ParamDecl> params() const override {
        return { center.decl(), axis.decl(), strength.decl(), falloff.decl(), minDistance.decl() };
    }

    bool getParam(const std::string& paramName, float out[4]) const override {
        if (paramName == "center") {
            out[0] = center.x(); out[1] = center.y(); out[2] = center.z();
            return true;
        }
        if (paramName == "axis") {
            out[0] = axis.x(); out[1] = axis.y(); out[2] = axis.z();
            return true;
        }
        if (paramName == "strength") { out[0] = strength; return true; }
        if (paramName == "falloff") { out[0] = falloff; return true; }
        if (paramName == "minDistance") { out[0] = minDistance; return true; }
        return false;
    }

    bool setParam(const std::string& paramName, const float value[4]) override {
        if (paramName == "center") { center.set(value[0], value[1], value[2]); return true; }
        if (paramName == "axis") { axis.set(value[0], value[1], value[2]); return true; }
        if (paramName == "strength") { strength = value[0]; return true; }
        if (paramName == "falloff") { falloff = value[0]; return true; }
        if (paramName == "minDistance") { minDistance = value[0]; return true; }
        return false;
    }
};

} // namespace vivid::effects
