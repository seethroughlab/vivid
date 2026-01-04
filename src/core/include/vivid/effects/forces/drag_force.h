#pragma once

/**
 * @file drag_force.h
 * @brief Velocity damping force
 */

#include <vivid/effects/particle_forces.h>

namespace vivid::effects {

/**
 * @brief Velocity damping (drag)
 *
 * Reduces particle velocity over time. Applied as a multiplicative
 * factor rather than an additive force.
 *
 * vel *= (1 - drag * dt)
 */
class DragForce : public ParticleForce {
public:
    Param<float> coefficient{"coefficient", 0.0f, 0.0f, 5.0f};

    ForceType type() const override { return ForceType::Drag; }
    std::string name() const override { return "Drag"; }

    // Drag is special - returns zero and is handled in simulation loop
    glm::vec3 compute(const Particle& p, float time, float dt) override {
        return glm::vec3(0.0f);  // Applied as velocity multiplier, not force
    }

    // Flag to indicate drag needs special handling
    bool isVelocityMultiplier() const { return true; }

    // Returns the drag factor to multiply velocity by
    float getDragFactor(float dt) const {
        return 1.0f - static_cast<float>(coefficient) * dt;
    }

    std::string wgslUniformFields() const override {
        return "  drag: f32,\n"
               "  _dragPad1: f32,\n"
               "  _dragPad2: f32,\n"
               "  _dragPad3: f32,\n";
    }

    std::string wgslComputeCode(const std::string& u) const override {
        return "// Drag\n"
               "if (" + u + ".drag > 0.001) {\n"
               "    vel *= 1.0 - " + u + ".drag * dt;\n"
               "}\n";
    }

    size_t uniformSize() const override { return 16; }

    void writeUniforms(void* dest) const override {
        float* f = static_cast<float*>(dest);
        f[0] = static_cast<float>(coefficient);
        f[1] = 0.0f;
        f[2] = 0.0f;
        f[3] = 0.0f;
    }

    std::vector<ParamDecl> params() const override {
        return { coefficient.decl() };
    }

    bool getParam(const std::string& paramName, float out[4]) const override {
        if (paramName == "coefficient") { out[0] = coefficient; return true; }
        return false;
    }

    bool setParam(const std::string& paramName, const float value[4]) override {
        if (paramName == "coefficient") { coefficient = value[0]; return true; }
        return false;
    }
};

} // namespace vivid::effects
