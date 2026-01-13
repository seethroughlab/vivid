#pragma once

/**
 * @file volumetric_lighting.h
 * @brief Volumetric lighting post-process effect (god rays, light shafts)
 *
 * Ray marching-based volumetric lighting that simulates light scattering
 * through fog/dust/atmosphere.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>
#include <glm/glm.hpp>

namespace vivid::render3d {

class Render3D;
class LightOperator;
class CameraOperator;

/**
 * @brief Volumetric lighting post-process effect
 *
 * Adds god rays and light shafts to a 3D scene using ray marching.
 * Requires Render3D with depth output enabled.
 *
 * @par Example
 * @code
 * auto& render = chain.add<Render3D>("render");
 * render.setDepthOutput(true);  // Required!
 *
 * auto& volumetric = chain.add<VolumetricLighting>("volumetric");
 * volumetric.input(&render);
 * volumetric.lightInput(&streetlight);
 * volumetric.density = 0.05f;
 * volumetric.intensity = 2.0f;
 * volumetric.raySteps = 48;
 * @endcode
 *
 * @see Render3D, Fog, PointLight, SpotLight
 */
class VolumetricLighting : public effects::TextureOperator {
public:
    VolumetricLighting();
    ~VolumetricLighting();

    // -------------------------------------------------------------------------
    /// @name Input Configuration
    /// @{

    /// Set the Render3D input (must have depth output enabled)
    void input(Render3D* render);

    /// Set the primary light source for volumetrics
    void lightInput(LightOperator* light);

    /// Set the camera (required for world-space reconstruction)
    void cameraInput(CameraOperator* camera);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    /// Number of ray march steps (8-128, default 48)
    /// Higher = better quality, lower performance
    Param<int> raySteps{"raySteps", 48, 8, 128};

    /// Maximum ray march distance in world units
    Param<float> maxDistance{"maxDistance", 50.0f, 1.0f, 200.0f};

    /// Fog/medium density (0-0.5, default 0.03)
    Param<float> density{"density", 0.03f, 0.0f, 0.5f};

    /// Scattering intensity multiplier (0-10, default 1.5)
    Param<float> intensity{"intensity", 1.5f, 0.0f, 10.0f};

    /// Scattering anisotropy for Henyey-Greenstein phase function
    /// -1 = back scatter, 0 = isotropic, 1 = forward scatter
    Param<float> anisotropy{"anisotropy", 0.7f, -1.0f, 1.0f};

    /// Fog color tint (default: slight blue-gray for night fog)
    float fogColor[3] = {0.02f, 0.02f, 0.03f};

    /// Debug mode (0=off, 1=depth, 2=world pos, 3=distance, 4=light contrib, 5=passthrough)
    Param<int> debugMode{"debugMode", 0, 0, 5};

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "VolumetricLighting"; }

    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}

private:
    void createPipeline(Context& ctx);

    Render3D* m_render3d = nullptr;
    LightOperator* m_lightOp = nullptr;
    CameraOperator* m_cameraOp = nullptr;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
};

} // namespace vivid::render3d
