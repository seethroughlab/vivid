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
 * - **epsilon**: Finite difference step size (smaller = finer detail)
 * - **lacunarity**: Frequency multiplier per octave (typically 2.0)
 * - **persistence**: Amplitude multiplier per octave (typically 0.5)
 */
class CurlNoiseForce : public ParticleForce {
public:
    Param<float> strength{"strength", 0.0f, 0.0f, 5.0f};
    Param<float> scale{"scale", 4.0f, 0.1f, 20.0f};
    Param<float> speed{"speed", 0.3f, 0.0f, 2.0f};
    Param<int> octaves{"octaves", 3, 1, 6};
    Param<float> epsilon{"epsilon", 0.1f, 0.001f, 1.0f};       // Finite difference step
    Param<float> lacunarity{"lacunarity", 2.0f, 1.5f, 4.0f};   // Frequency multiplier
    Param<float> persistence{"persistence", 0.5f, 0.1f, 1.0f}; // Amplitude multiplier

    ForceType type() const override { return ForceType::CurlNoise; }
    std::string name() const override { return "CurlNoise"; }

    // CPU computation - requires external noise helpers (snoise3, fbm3)
    glm::vec3 compute(const Particle& p, float time, float dt) override;

    // GPU shader generation
    std::string wgslUniformFields() const override {
        return "  curlStrength: f32,\n"
               "  curlScale: f32,\n"
               "  curlSpeed: f32,\n"
               "  curlOctaves: i32,\n"
               "  curlEpsilon: f32,\n"
               "  curlLacunarity: f32,\n"
               "  curlPersistence: f32,\n"
               "  _curlPad: f32,\n";  // Padding for 16-byte alignment
    }

    std::string wgslComputeCode(const std::string& u) const override {
        return "// Curl noise\n"
               "if (" + u + ".curlStrength > 0.001) {\n"
               "    let curlTime = time * " + u + ".curlSpeed + seed * 10.0;\n"
               "    let eps = " + u + ".curlEpsilon;\n"
               "    let lac = " + u + ".curlLacunarity;\n"
               "    let pers = " + u + ".curlPersistence;\n"
               "    if (is3D) {\n"
               "        let curl = curlNoise3DEx(pos, curlTime, " + u + ".curlScale, " + u + ".curlOctaves, eps, lac, pers);\n"
               "        vel += curl * " + u + ".curlStrength * dt;\n"
               "    } else {\n"
               "        let curl2D = curlNoise2DEx(pos.xy, curlTime, " + u + ".curlScale, " + u + ".curlOctaves, eps, lac, pers);\n"
               "        vel.x += curl2D.x * " + u + ".curlStrength * dt;\n"
               "        vel.y += curl2D.y * " + u + ".curlStrength * dt;\n"
               "    }\n"
               "}\n";
    }

    size_t uniformSize() const override { return 32; }  // 8 floats, 16-byte aligned

    void writeUniforms(void* dest) const override {
        float* f = static_cast<float*>(dest);
        f[0] = static_cast<float>(strength);
        f[1] = static_cast<float>(scale);
        f[2] = static_cast<float>(speed);
        // Write int as float bits (for WGSL i32)
        int oct = static_cast<int>(octaves);
        std::memcpy(&f[3], &oct, sizeof(int));
        f[4] = static_cast<float>(epsilon);
        f[5] = static_cast<float>(lacunarity);
        f[6] = static_cast<float>(persistence);
        f[7] = 0.0f;  // padding
    }

    std::vector<ParamDecl> params() const override {
        return {
            strength.decl(),
            scale.decl(),
            speed.decl(),
            octaves.decl(),
            epsilon.decl(),
            lacunarity.decl(),
            persistence.decl()
        };
    }

    bool getParam(const std::string& paramName, float out[4]) const override {
        if (paramName == "strength") { out[0] = strength; return true; }
        if (paramName == "scale") { out[0] = scale; return true; }
        if (paramName == "speed") { out[0] = speed; return true; }
        if (paramName == "octaves") { out[0] = static_cast<float>(static_cast<int>(octaves)); return true; }
        if (paramName == "epsilon") { out[0] = epsilon; return true; }
        if (paramName == "lacunarity") { out[0] = lacunarity; return true; }
        if (paramName == "persistence") { out[0] = persistence; return true; }
        return false;
    }

    bool setParam(const std::string& paramName, const float value[4]) override {
        if (paramName == "strength") { strength = value[0]; return true; }
        if (paramName == "scale") { scale = value[0]; return true; }
        if (paramName == "speed") { speed = value[0]; return true; }
        if (paramName == "octaves") { octaves = static_cast<int>(value[0]); return true; }
        if (paramName == "epsilon") { epsilon = value[0]; return true; }
        if (paramName == "lacunarity") { lacunarity = value[0]; return true; }
        if (paramName == "persistence") { persistence = value[0]; return true; }
        return false;
    }

    // Track if 3D mode for CPU computation
    bool is3D = true;
};

} // namespace vivid::effects
