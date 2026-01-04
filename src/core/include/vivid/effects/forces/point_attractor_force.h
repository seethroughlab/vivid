#pragma once

/**
 * @file point_attractor_force.h
 * @brief Point attractor/repulsor force
 */

#include <vivid/effects/particle_forces.h>

namespace vivid::effects {

/**
 * @brief Point attractor/repulsor
 *
 * Attracts or repels particles from a point in space.
 * Force falls off with distance (inverse distance).
 *
 * - Positive strength = attract
 * - Negative strength = repel
 */
class PointAttractorForce : public ParticleForce {
public:
    Vec3Param position{"position", 0.5f, 0.5f, 0.0f, -10.0f, 10.0f};
    Param<float> strength{"strength", 0.0f, -10.0f, 10.0f};

    ForceType type() const override { return ForceType::PointAttractor; }
    std::string name() const override { return "PointAttractor"; }

    glm::vec3 compute(const Particle& p, float time, float dt) override {
        float s = static_cast<float>(strength);
        if (std::abs(s) < 0.001f) return glm::vec3(0.0f);

        glm::vec3 attPos(position.x(), position.y(), position.z());
        glm::vec3 toAtt = attPos - p.position;
        float dist = glm::length(toAtt);

        if (dist < 0.01f) return glm::vec3(0.0f);

        return glm::normalize(toAtt) * s * dt / dist;
    }

    std::string wgslUniformFields() const override {
        return "  attractorX: f32,\n"
               "  attractorY: f32,\n"
               "  attractorZ: f32,\n"
               "  attractorStrength: f32,\n";
    }

    std::string wgslComputeCode(const std::string& u) const override {
        return "// Attractor\n"
               "if (abs(" + u + ".attractorStrength) > 0.001) {\n"
               "    let attPos = vec3f(" + u + ".attractorX, " + u + ".attractorY, " + u + ".attractorZ);\n"
               "    let toAtt = attPos - pos;\n"
               "    let dist = length(toAtt);\n"
               "    if (dist > 0.01) {\n"
               "        vel += normalize(toAtt) * " + u + ".attractorStrength * dt / dist;\n"
               "    }\n"
               "}\n";
    }

    size_t uniformSize() const override { return 16; }

    void writeUniforms(void* dest) const override {
        float* f = static_cast<float*>(dest);
        f[0] = position.x();
        f[1] = position.y();
        f[2] = position.z();
        f[3] = static_cast<float>(strength);
    }

    std::vector<ParamDecl> params() const override {
        return { position.decl(), strength.decl() };
    }

    bool getParam(const std::string& paramName, float out[4]) const override {
        if (paramName == "position") {
            out[0] = position.x();
            out[1] = position.y();
            out[2] = position.z();
            return true;
        }
        if (paramName == "strength") { out[0] = strength; return true; }
        return false;
    }

    bool setParam(const std::string& paramName, const float value[4]) override {
        if (paramName == "position") {
            position.set(value[0], value[1], value[2]);
            return true;
        }
        if (paramName == "strength") { strength = value[0]; return true; }
        return false;
    }
};

} // namespace vivid::effects
