#pragma once

/**
 * @file turbulence_force.h
 * @brief Random jitter/turbulence force
 */

#include <vivid/effects/particle_forces.h>
#include <random>

namespace vivid::effects {

/**
 * @brief Random jitter force
 *
 * Adds random velocity changes each frame, creating
 * a jittery, turbulent motion.
 */
class TurbulenceForce : public ParticleForce {
public:
    Param<float> strength{"strength", 0.0f, 0.0f, 5.0f};

    ForceType type() const override { return ForceType::Turbulence; }
    std::string name() const override { return "Turbulence"; }

    glm::vec3 compute(const Particle& p, float time, float dt) override {
        float s = static_cast<float>(strength);
        if (s < 0.0001f) return glm::vec3(0.0f);

        // Simple hash-based random using particle seed and time
        auto hash = [](float x) {
            x = std::fmod(x * 0.1031f, 1.0f);
            x *= x + 33.33f;
            return std::fmod((x + x) * x, 1.0f);
        };

        float seedVal = p.seed * 1000.0f + time * 100.0f;
        glm::vec3 jitter(
            hash(seedVal) * 2.0f - 1.0f,
            hash(seedVal + 100.0f) * 2.0f - 1.0f,
            is3D ? (hash(seedVal + 200.0f) * 2.0f - 1.0f) : 0.0f
        );

        return jitter * s * dt;
    }

    std::string wgslUniformFields() const override {
        return "  turbulence: f32,\n"
               "  _turbPad1: f32,\n"
               "  _turbPad2: f32,\n"
               "  _turbPad3: f32,\n";
    }

    std::string wgslComputeCode(const std::string& u) const override {
        return "// Turbulence (random jitter)\n"
               "if (" + u + ".turbulence > 0.001) {\n"
               "    let turbSeed = vec3f(seed * 1000.0, time * 100.0, f32(idx));\n"
               "    let jitter = vec3f(\n"
               "        hash31(turbSeed) * 2.0 - 1.0,\n"
               "        hash31(turbSeed + vec3f(100.0, 0.0, 0.0)) * 2.0 - 1.0,\n"
               "        hash31(turbSeed + vec3f(0.0, 100.0, 0.0)) * 2.0 - 1.0\n"
               "    );\n"
               "    if (is3D) {\n"
               "        vel += jitter * " + u + ".turbulence * dt;\n"
               "    } else {\n"
               "        vel.x += jitter.x * " + u + ".turbulence * dt;\n"
               "        vel.y += jitter.y * " + u + ".turbulence * dt;\n"
               "    }\n"
               "}\n";
    }

    size_t uniformSize() const override { return 16; }

    void writeUniforms(void* dest) const override {
        float* f = static_cast<float*>(dest);
        f[0] = static_cast<float>(strength);
        f[1] = 0.0f;
        f[2] = 0.0f;
        f[3] = 0.0f;
    }

    std::vector<ParamDecl> params() const override {
        return { strength.decl() };
    }

    bool getParam(const std::string& paramName, float out[4]) const override {
        if (paramName == "strength") { out[0] = strength; return true; }
        return false;
    }

    bool setParam(const std::string& paramName, const float value[4]) override {
        if (paramName == "strength") { strength = value[0]; return true; }
        return false;
    }

    bool is3D = true;
};

} // namespace vivid::effects
