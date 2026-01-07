#pragma once

/**
 * @file wind_force.h
 * @brief Directional wind with turbulent gusts
 */

#include <vivid/effects/particle_forces.h>

namespace vivid::effects {

/**
 * @brief Directional wind force with gusts
 *
 * Applies constant directional push with optional turbulent gusts.
 * Gusts use Perlin noise for natural variation.
 *
 * @par Parameters
 * - **direction**: Wind direction (will be normalized)
 * - **strength**: Base wind strength
 * - **gustStrength**: Additional strength from gusts (0-1 multiplier)
 * - **gustFrequency**: How fast gusts change (higher = more rapid)
 */
class WindForce : public ParticleForce {
public:
    Vec3Param direction{"direction", 1.0f, 0.0f, 0.0f, -1.0f, 1.0f};
    Param<float> strength{"strength", 0.0f, 0.0f, 10.0f};
    Param<float> gustStrength{"gustStrength", 0.0f, 0.0f, 1.0f};
    Param<float> gustFrequency{"gustFrequency", 1.0f, 0.0f, 10.0f};

    ForceType type() const override { return ForceType::Wind; }
    std::string name() const override { return "Wind"; }

    glm::vec3 compute(const Particle& p, float time, float dt) override {
        float s = static_cast<float>(strength);
        if (std::abs(s) < 0.001f) return glm::vec3(0.0f);

        glm::vec3 dir = glm::normalize(glm::vec3(direction.x(), direction.y(), direction.z()));
        float gust = static_cast<float>(gustStrength);
        float freq = static_cast<float>(gustFrequency);

        // Base wind
        float force = s;

        // Add gust variation using simple noise
        if (gust > 0.001f) {
            // Use particle position and time for spatial/temporal variation
            float noiseInput = p.position.x * freq + p.position.y * freq + time * freq;
            float gustValue = std::sin(noiseInput * 6.28318f) * 0.5f + 0.5f;  // 0-1
            force *= (1.0f + gustValue * gust);
        }

        return dir * force * dt;
    }

    std::string wgslUniformFields() const override {
        return "  windDirX: f32,\n"
               "  windDirY: f32,\n"
               "  windDirZ: f32,\n"
               "  windStrength: f32,\n"
               "  windGustStrength: f32,\n"
               "  windGustFrequency: f32,\n"
               "  _windPad0: f32,\n"
               "  _windPad1: f32,\n";
    }

    std::string wgslComputeCode(const std::string& u) const override {
        return "// Wind\n"
               "if (abs(" + u + ".windStrength) > 0.001) {\n"
               "    let windDir = normalize(vec3f(" + u + ".windDirX, " + u + ".windDirY, " + u + ".windDirZ));\n"
               "    var windForce = " + u + ".windStrength;\n"
               "    if (" + u + ".windGustStrength > 0.001) {\n"
               "        let noiseInput = pos.x * " + u + ".windGustFrequency + pos.y * " + u + ".windGustFrequency + u.time * " + u + ".windGustFrequency;\n"
               "        let gustValue = sin(noiseInput * 6.28318) * 0.5 + 0.5;\n"
               "        windForce *= (1.0 + gustValue * " + u + ".windGustStrength);\n"
               "    }\n"
               "    vel += windDir * windForce * dt;\n"
               "}\n";
    }

    size_t uniformSize() const override { return 32; }  // 8 floats

    void writeUniforms(void* dest) const override {
        float* f = static_cast<float*>(dest);
        glm::vec3 dir = glm::normalize(glm::vec3(direction.x(), direction.y(), direction.z()));
        f[0] = dir.x;
        f[1] = dir.y;
        f[2] = dir.z;
        f[3] = static_cast<float>(strength);
        f[4] = static_cast<float>(gustStrength);
        f[5] = static_cast<float>(gustFrequency);
        f[6] = 0.0f;  // padding
        f[7] = 0.0f;  // padding
    }

    std::vector<ParamDecl> params() const override {
        return { direction.decl(), strength.decl(), gustStrength.decl(), gustFrequency.decl() };
    }

    bool getParam(const std::string& paramName, float out[4]) const override {
        if (paramName == "direction") {
            out[0] = direction.x(); out[1] = direction.y(); out[2] = direction.z();
            return true;
        }
        if (paramName == "strength") { out[0] = strength; return true; }
        if (paramName == "gustStrength") { out[0] = gustStrength; return true; }
        if (paramName == "gustFrequency") { out[0] = gustFrequency; return true; }
        return false;
    }

    bool setParam(const std::string& paramName, const float value[4]) override {
        if (paramName == "direction") { direction.set(value[0], value[1], value[2]); return true; }
        if (paramName == "strength") { strength = value[0]; return true; }
        if (paramName == "gustStrength") { gustStrength = value[0]; return true; }
        if (paramName == "gustFrequency") { gustFrequency = value[0]; return true; }
        return false;
    }
};

} // namespace vivid::effects
