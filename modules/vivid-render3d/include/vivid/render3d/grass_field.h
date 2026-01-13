#pragma once

/**
 * @file grass_field.h
 * @brief GPU-instanced grass field with wind animation
 *
 * @deprecated Use GrassMesh + Render3D instead for unified shadow support
 * and proper depth testing. GrassField renders standalone and cannot
 * participate in Render3D shadow maps.
 *
 * Migration example:
 * @code
 * // OLD (GrassField - standalone rendering)
 * auto& grass = chain.add<GrassField>("grass");
 * grass.setCameraInput(&camera);
 * grass.setLightInput(&light);
 *
 * // NEW (GrassMesh + Render3D - unified shadows)
 * auto& grass = chain.add<GrassMesh>("grass");
 * render.addProceduralMesh(&grass);  // Render3D handles rendering
 * @endcode
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/render3d/camera_operator.h>
#include <vivid/render3d/light_operators.h>
#include <vivid/render3d/mesh.h>
#include <vivid/param.h>
#include <glm/glm.hpp>
#include <vector>

namespace vivid::render3d {

/**
 * @brief GPU-instanced grass field with wind animation
 *
 * @deprecated Use GrassMesh + Render3D instead. See file header for migration.
 *
 * Renders a field of grass blades with realistic wind animation.
 * Each blade is a tapered quad strip that bends based on wind parameters.
 * Uses GPU instancing for efficient rendering of thousands of blades.
 */
class [[deprecated("Use GrassMesh + Render3D for unified shadow support")]]
GrassField : public vivid::effects::TextureOperator {
public:
    GrassField();
    ~GrassField() override;

    // =========================================================================
    /// @name Field Parameters
    /// @{

    /// Width of the grass field (X axis)
    Param<float> fieldWidth{"fieldWidth", 10.0f, 1.0f, 100.0f};

    /// Depth of the grass field (Z axis)
    Param<float> fieldDepth{"fieldDepth", 10.0f, 1.0f, 100.0f};

    /// Number of grass blades
    Param<int> bladeCount{"bladeCount", 5000, 100, 50000};

    /// Random seed for blade placement
    Param<int> seed{"seed", 12345, 0, 99999};

    /// @}
    // =========================================================================
    /// @name Blade Parameters
    /// @{

    /// Average height of grass blades
    Param<float> bladeHeight{"bladeHeight", 0.4f, 0.1f, 2.0f};

    /// Width of grass blades at base
    Param<float> bladeWidth{"bladeWidth", 0.03f, 0.005f, 0.2f};

    /// Height variation (0-1, multiplied with bladeHeight)
    Param<float> heightVariation{"heightVariation", 0.3f, 0.0f, 1.0f};

    /// @}
    // =========================================================================
    /// @name Wind Parameters
    /// @{

    /// Wind strength (displacement amount)
    Param<float> windStrength{"windStrength", 0.5f, 0.0f, 2.0f};

    /// Wind speed (animation rate)
    Param<float> windSpeed{"windSpeed", 1.0f, 0.1f, 5.0f};

    /// Wind direction X component
    Param<float> windDirX{"windDirX", 1.0f, -1.0f, 1.0f};

    /// Wind direction Z component
    Param<float> windDirZ{"windDirZ", 0.3f, -1.0f, 1.0f};

    /// @}
    // =========================================================================
    /// @name Color Parameters
    /// @{

    /// Base color (at blade root)
    float baseColor[3] = {0.15f, 0.35f, 0.08f};

    /// Tip color (at blade tip)
    float tipColor[3] = {0.3f, 0.55f, 0.15f};

    /// @}
    // =========================================================================
    /// @name Inputs
    /// @{

    /// Set camera input for view/projection
    void setCameraInput(CameraOperator* cam);

    /// Set light input for shading
    void setLightInput(LightOperator* light);

    /// Add additional light (up to 4 total)
    void addLight(LightOperator* light);

    /// @}
    // =========================================================================
    /// @name Rendering Options
    /// @{

    /// Set clear color for the render target
    void setClearColor(float r, float g, float b, float a = 1.0f);

    /// Set render resolution
    void setResolution(int width, int height);

    /// @}
    // =========================================================================
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "GrassField"; }

    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}

private:
    void createBladeMesh();
    void generateInstances();
    void createPipeline(Context& ctx);
    void createDepthBuffer(Context& ctx);
    void uploadInstances(Context& ctx);

    // Inputs
    CameraOperator* m_cameraOp = nullptr;
    std::vector<LightOperator*> m_lightOps;

    // Blade mesh
    Mesh m_bladeMesh;
    bool m_meshCreated = false;

    // Instance data
    struct GrassInstance {
        glm::mat4 transform;
        glm::vec4 color;      // RGB + phase offset in alpha
    };
    std::vector<GrassInstance> m_instances;
    bool m_instancesDirty = true;
    int m_lastBladeCount = 0;
    int m_lastSeed = 0;

    // Rendering options
    glm::vec4 m_clearColor{0.4f, 0.6f, 0.9f, 1.0f};  // Sky blue default

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUBuffer m_vertexBuffer = nullptr;
    WGPUBuffer m_indexBuffer = nullptr;
    WGPUBuffer m_instanceBuffer = nullptr;
    size_t m_instanceCapacity = 0;

    // Depth buffer
    WGPUTexture m_depthTexture = nullptr;
    WGPUTextureView m_depthView = nullptr;
    int m_depthWidth = 0;
    int m_depthHeight = 0;

    bool m_pipelineCreated = false;
    bool m_initialized = false;
};

} // namespace vivid::render3d
