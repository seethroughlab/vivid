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
 *
 * - Positive strength = attract
 * - Negative strength = repel
 *
 * @par Falloff Modes
 * - **radius = 0** (default): Inverse distance falloff (1/d), unbounded range
 * - **radius > 0**: Quadratic falloff 1-(d/r)², force smoothly reaches zero at radius
 */
class PointAttractorForce : public ParticleForce {
public:
    Vec3Param position{"position", 0.5f, 0.5f, 0.0f, -10.0f, 10.0f};
    Param<float> strength{"strength", 0.0f, -10.0f, 10.0f};
    Param<float> radius{"radius", 0.0f, 0.0f, 20.0f};  // 0 = unbounded (1/d), >0 = quadratic falloff

    ForceType type() const override { return ForceType::PointAttractor; }
    std::string name() const override { return "PointAttractor"; }

    glm::vec3 compute(const Particle& p, float time, float dt) override {
        float s = static_cast<float>(strength);
        if (std::abs(s) < 0.001f) return glm::vec3(0.0f);

        glm::vec3 attPos(position.x(), position.y(), position.z());
        glm::vec3 toAtt = attPos - p.position;
        float dist = glm::length(toAtt);

        if (dist < 0.01f) return glm::vec3(0.0f);

        float r = static_cast<float>(radius);
        float falloff;
        if (r > 0.0f) {
            // Quadratic falloff: 1 - (d/r)², smoothly reaches zero at radius
            if (dist >= r) return glm::vec3(0.0f);
            float t = dist / r;
            falloff = 1.0f - t * t;
        } else {
            // Classic inverse distance falloff
            falloff = 1.0f / dist;
        }

        return glm::normalize(toAtt) * s * falloff * dt;
    }

    std::string wgslUniformFields() const override {
        return "  attractorX: f32,\n"
               "  attractorY: f32,\n"
               "  attractorZ: f32,\n"
               "  attractorStrength: f32,\n"
               "  attractorRadius: f32,\n"
               "  _attractorPad1: f32,\n"
               "  _attractorPad2: f32,\n"
               "  _attractorPad3: f32,\n";  // Pad to 32 bytes (8 floats)
    }

    std::string wgslComputeCode(const std::string& u) const override {
        return "// Attractor\n"
               "if (abs(" + u + ".attractorStrength) > 0.001) {\n"
               "    let attPos = vec3f(" + u + ".attractorX, " + u + ".attractorY, " + u + ".attractorZ);\n"
               "    let toAtt = attPos - pos;\n"
               "    let dist = length(toAtt);\n"
               "    if (dist > 0.01) {\n"
               "        var falloff: f32;\n"
               "        if (" + u + ".attractorRadius > 0.0) {\n"
               "            // Quadratic falloff: 1 - (d/r)²\n"
               "            let t = dist / " + u + ".attractorRadius;\n"
               "            if (t >= 1.0) { falloff = 0.0; }\n"
               "            else { falloff = 1.0 - t * t; }\n"
               "        } else {\n"
               "            // Classic inverse distance\n"
               "            falloff = 1.0 / dist;\n"
               "        }\n"
               "        vel += normalize(toAtt) * " + u + ".attractorStrength * falloff * dt;\n"
               "    }\n"
               "}\n";
    }

    size_t uniformSize() const override { return 32; }  // 8 floats for alignment

    void writeUniforms(void* dest) const override {
        float* f = static_cast<float*>(dest);
        f[0] = position.x();
        f[1] = position.y();
        f[2] = position.z();
        f[3] = static_cast<float>(strength);
        f[4] = static_cast<float>(radius);
        f[5] = 0.0f;  // padding
        f[6] = 0.0f;
        f[7] = 0.0f;
    }

    std::vector<ParamDecl> params() const override {
        return { position.decl(), strength.decl(), radius.decl() };
    }

    bool getParam(const std::string& paramName, float out[4]) const override {
        if (paramName == "position") {
            out[0] = position.x();
            out[1] = position.y();
            out[2] = position.z();
            return true;
        }
        if (paramName == "strength") { out[0] = strength; return true; }
        if (paramName == "radius") { out[0] = radius; return true; }
        return false;
    }

    bool setParam(const std::string& paramName, const float value[4]) override {
        if (paramName == "position") {
            position.set(value[0], value[1], value[2]);
            return true;
        }
        if (paramName == "strength") { strength = value[0]; return true; }
        if (paramName == "radius") { radius = value[0]; return true; }
        return false;
    }
};

} // namespace vivid::effects
