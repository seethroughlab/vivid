#pragma once

/**
 * @file curl_noise_force.h
 * @brief Curl noise force field - creates organic, divergence-free flow
 */

#include <vivid/effects/particle_forces.h>
#include <cstring>  // for std::memcpy

namespace vivid::effects {

/**
 * @brief Curl noise force field
 *
 * Creates organic, swirling motion using the curl of a noise function.
 * The resulting velocity field is divergence-free, meaning particles
 * don't compress or expand - they flow smoothly like a fluid.
 *
 * @par Parameters
 * - **strength**: Force multiplier (0-5)
 * - **scale**: Frequency/detail of the noise pattern (0.1-20)
 * - **speed**: Animation speed of the noise (0-2)
 * - **octaves**: FBM complexity layers (1-6)
 */
class CurlNoiseForce : public ParticleForce {
public:
    Param<float> strength{"strength", 0.0f, 0.0f, 5.0f};
    Param<float> scale{"scale", 4.0f, 0.1f, 20.0f};
    Param<float> speed{"speed", 0.3f, 0.0f, 2.0f};
    Param<int> octaves{"octaves", 3, 1, 6};

    ForceType type() const override { return ForceType::CurlNoise; }
    std::string name() const override { return "CurlNoise"; }

    // CPU computation - requires external noise helpers (snoise3, fbm3)
    glm::vec3 compute(const Particle& p, float time, float dt) override;

    // GPU shader generation
    std::string wgslUniformFields() const override {
        return "  curlStrength: f32,\n"
               "  curlScale: f32,\n"
               "  curlSpeed: f32,\n"
               "  curlOctaves: i32,\n";
    }

    std::string wgslComputeCode(const std::string& u) const override {
        return "// Curl noise\n"
               "if (" + u + ".curlStrength > 0.001) {\n"
               "    let curlTime = time * " + u + ".curlSpeed + seed * 10.0;\n"
               "    if (is3D) {\n"
               "        let curl = curlNoise3D(pos, curlTime, " + u + ".curlScale, " + u + ".curlOctaves);\n"
               "        vel += curl * " + u + ".curlStrength * dt;\n"
               "    } else {\n"
               "        let curl2D = curlNoise2D(pos.xy, curlTime, " + u + ".curlScale, " + u + ".curlOctaves);\n"
               "        vel.x += curl2D.x * " + u + ".curlStrength * dt;\n"
               "        vel.y += curl2D.y * " + u + ".curlStrength * dt;\n"
               "    }\n"
               "}\n";
    }

    size_t uniformSize() const override { return 16; }  // 4 floats, 16-byte aligned

    void writeUniforms(void* dest) const override {
        float* f = static_cast<float*>(dest);
        f[0] = static_cast<float>(strength);
        f[1] = static_cast<float>(scale);
        f[2] = static_cast<float>(speed);
        // Write int as float bits (for WGSL i32)
        int oct = static_cast<int>(octaves);
        std::memcpy(&f[3], &oct, sizeof(int));
    }

    std::vector<ParamDecl> params() const override {
        return {
            strength.decl(),
            scale.decl(),
            speed.decl(),
            octaves.decl()
        };
    }

    bool getParam(const std::string& paramName, float out[4]) const override {
        if (paramName == "strength") { out[0] = strength; return true; }
        if (paramName == "scale") { out[0] = scale; return true; }
        if (paramName == "speed") { out[0] = speed; return true; }
        if (paramName == "octaves") { out[0] = static_cast<float>(static_cast<int>(octaves)); return true; }
        return false;
    }

    bool setParam(const std::string& paramName, const float value[4]) override {
        if (paramName == "strength") { strength = value[0]; return true; }
        if (paramName == "scale") { scale = value[0]; return true; }
        if (paramName == "speed") { speed = value[0]; return true; }
        if (paramName == "octaves") { octaves = static_cast<int>(value[0]); return true; }
        return false;
    }

    // Track if 3D mode for CPU computation
    bool is3D = true;
};

} // namespace vivid::effects
