#pragma once

/**
 * @file foliage_cluster.h
 * @brief GPU-instanced procedural fronds with wind animation
 *
 * @deprecated Use FoliageMesh + Render3D instead for unified shadow support
 * and proper depth testing. FoliageCluster renders standalone and cannot
 * participate in Render3D shadow maps.
 *
 * Migration example:
 * @code
 * // OLD (FoliageCluster - standalone rendering)
 * auto& foliage = chain.add<FoliageCluster>("foliage");
 * foliage.setCameraInput(&camera);
 * foliage.setLightInput(&light);
 *
 * // NEW (FoliageMesh + Render3D - unified shadows)
 * auto& foliage = chain.add<FoliageMesh>("foliage");
 * render.addProceduralMesh(&foliage);  // Render3D handles rendering
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
 * @brief GPU-instanced procedural fronds with wind animation
 *
 * @deprecated Use FoliageMesh + Render3D instead. See file header for migration.
 *
 * Generates plant geometry with curved stems and tapered leaflets.
 * Each frond is a single mesh with natural branching structure.
 */
class [[deprecated("Use FoliageMesh + Render3D for unified shadow support")]]
FoliageCluster : public vivid::effects::TextureOperator {
public:
    /**
     * @brief Plant type presets with tuned geometry and animation
     */
    enum class PlantType {
        Fern,       ///< Many small pinnae, moderate droop
        PalmFrond,  ///< Fewer large pinnae, heavy droop
        Grass,      ///< Simple tapered blade (like GrassField)
        Custom      ///< User-defined parameters
    };

    FoliageCluster();
    ~FoliageCluster() override;

    // =========================================================================
    /// @name Plant Type
    /// @{

    /// Set plant type preset (adjusts geometry and animation)
    void setPlantType(PlantType type);

    /// Get current plant type
    PlantType plantType() const { return m_plantType; }

    /// @}
    // =========================================================================
    /// @name Field Parameters
    /// @{

    /// Width of the foliage field (X axis)
    Param<float> fieldWidth{"fieldWidth", 10.0f, 1.0f, 100.0f};

    /// Depth of the foliage field (Z axis)
    Param<float> fieldDepth{"fieldDepth", 10.0f, 1.0f, 100.0f};

    /// Number of fronds
    Param<int> frondCount{"frondCount", 150, 10, 1000};

    /// Random seed for placement
    Param<int> seed{"seed", 12345, 0, 99999};

    /// Base height (Y offset for all fronds)
    Param<float> baseHeight{"baseHeight", 0.0f, -10.0f, 20.0f};

    /// @}
    // =========================================================================
    /// @name Frond Geometry
    /// @{

    /// Length of the frond stem
    Param<float> stemLength{"stemLength", 1.0f, 0.2f, 3.0f};

    /// Curvature/droop of the stem (0=straight, 1=heavy droop)
    Param<float> stemCurve{"stemCurve", 0.4f, 0.0f, 1.0f};

    /// Number of leaflet pairs along the stem
    Param<int> leafletPairs{"leafletPairs", 8, 2, 20};

    /// Width of leaflets at base
    Param<float> leafletWidth{"leafletWidth", 0.15f, 0.02f, 0.5f};

    /// Length of leaflets
    Param<float> leafletLength{"leafletLength", 0.25f, 0.05f, 0.8f};

    /// Angle of leaflets from stem (degrees)
    Param<float> leafletAngle{"leafletAngle", 45.0f, 10.0f, 80.0f};

    /// Size variation (0-1)
    Param<float> sizeVariation{"sizeVariation", 0.3f, 0.0f, 0.5f};

    /// @}
    // =========================================================================
    /// @name Wind Parameters
    /// @{

    /// Wind strength (displacement amount)
    Param<float> windStrength{"windStrength", 0.3f, 0.0f, 1.5f};

    /// Wind speed (animation rate)
    Param<float> windSpeed{"windSpeed", 0.8f, 0.1f, 3.0f};

    /// Wind direction X component
    Param<float> windDirX{"windDirX", 1.0f, -1.0f, 1.0f};

    /// Wind direction Z component
    Param<float> windDirZ{"windDirZ", 0.3f, -1.0f, 1.0f};

    /// @}
    // =========================================================================
    /// @name Color Parameters
    /// @{

    /// Base color (at stem/leaflet base)
    float baseColor[3] = {0.08f, 0.18f, 0.04f};

    /// Tip color (at leaflet tips)
    float tipColor[3] = {0.15f, 0.35f, 0.08f};

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
    std::string name() const override { return "FoliageCluster"; }

    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}

private:
    void generateFrondMesh();
    void generateInstances();
    void createPipeline(Context& ctx);
    void createDepthBuffer(Context& ctx);
    void uploadMesh(Context& ctx);
    void uploadInstances(Context& ctx);
    void applyPreset(PlantType type);

    // Plant type
    PlantType m_plantType = PlantType::Fern;

    // Inputs
    CameraOperator* m_cameraOp = nullptr;
    std::vector<LightOperator*> m_lightOps;

    // Procedural frond mesh
    Mesh m_frondMesh;
    bool m_meshDirty = true;
    // Track geometry params for change detection
    float m_lastStemLength = 0;
    float m_lastStemCurve = 0;
    int m_lastLeafletPairs = 0;
    float m_lastLeafletWidth = 0;
    float m_lastLeafletLength = 0;
    float m_lastLeafletAngle = 0;

    // Instance data
    struct FrondInstance {
        glm::mat4 transform;
        glm::vec4 variation;  // scale.xyz + phase in w
    };
    std::vector<FrondInstance> m_instances;
    bool m_instancesDirty = true;
    int m_lastFrondCount = 0;
    int m_lastSeed = 0;
    float m_lastBaseHeight = 0;
    float m_lastFieldWidth = 0;
    float m_lastFieldDepth = 0;
    float m_lastSizeVariation = 0;

    // Rendering options
    glm::vec4 m_clearColor{0.4f, 0.6f, 0.9f, 1.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUBuffer m_vertexBuffer = nullptr;
    WGPUBuffer m_indexBuffer = nullptr;
    WGPUBuffer m_instanceBuffer = nullptr;
    size_t m_vertexCapacity = 0;
    size_t m_indexCapacity = 0;
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
