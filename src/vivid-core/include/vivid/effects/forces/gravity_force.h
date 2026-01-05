#pragma once

/**
 * @file gravity_force.h
 * @brief Constant directional gravity force
 */

#include <vivid/effects/particle_forces.h>

namespace vivid::effects {

/**
 * @brief Constant directional gravity
 *
 * Applies a constant acceleration in a specified direction.
 * Default is downward (0, -9.8, 0) like Earth's gravity.
 */
class GravityForce : public ParticleForce {
public:
    Vec3Param direction{"direction", 0.0f, 0.0f, 0.0f, -20.0f, 20.0f};

    ForceType type() const override { return ForceType::Gravity; }
    std::string name() const override { return "Gravity"; }

    glm::vec3 compute(const Particle& p, float time, float dt) override {
        return glm::vec3(direction.x(), direction.y(), direction.z()) * dt;
    }

    std::string wgslUniformFields() const override {
        return "  gravityX: f32,\n"
               "  gravityY: f32,\n"
               "  gravityZ: f32,\n"
               "  _gravityPad: f32,\n";
    }

    std::string wgslComputeCode(const std::string& u) const override {
        return "// Gravity\n"
               "vel += vec3f(" + u + ".gravityX, " + u + ".gravityY, " + u + ".gravityZ) * dt;\n";
    }

    size_t uniformSize() const override { return 16; }

    void writeUniforms(void* dest) const override {
        float* f = static_cast<float*>(dest);
        f[0] = direction.x();
        f[1] = direction.y();
        f[2] = direction.z();
        f[3] = 0.0f;
    }

    std::vector<ParamDecl> params() const override {
        return { direction.decl() };
    }

    bool getParam(const std::string& paramName, float out[4]) const override {
        if (paramName == "direction") {
            out[0] = direction.x();
            out[1] = direction.y();
            out[2] = direction.z();
            return true;
        }
        return false;
    }

    bool setParam(const std::string& paramName, const float value[4]) override {
        if (paramName == "direction") {
            direction.set(value[0], value[1], value[2]);
            return true;
        }
        return false;
    }
};

} // namespace vivid::effects
