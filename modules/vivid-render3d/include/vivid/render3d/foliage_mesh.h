#pragma once

/**
 * @file foliage_mesh.h
 * @brief Procedural frond mesh generator
 *
 * Generates frond geometry with curved stems and tapered leaflets.
 * Used with Render3D for unified rendering with shadows and lighting.
 */

#include <vivid/render3d/procedural_mesh.h>
#include <vivid/param.h>
#include <glm/glm.hpp>
#include <vector>

namespace vivid::render3d {

/**
 * @brief Procedural frond mesh generator
 *
 * Generates frond geometry with curved stems (rachis) and tapered
 * leaflets (pinnae). This is a pure mesh generator - rendering is
 * handled by Render3D for unified shadows and lighting.
 *
 * @par Example
 * @code
 * auto& camera = chain.add<CameraOperator>("camera");
 * auto& sun = chain.add<DirectionalLight>("sun");
 *
 * auto& foliage = chain.add<FoliageMesh>("foliage");
 * foliage.setPlantType(FoliageMesh::PlantType::Fern);
 * foliage.frondCount = 200;
 *
 * auto& render = chain.add<Render3D>("render");
 * render.setCameraInput(&camera);
 * render.setLightInput(&sun);
 * render.addProceduralMesh(&foliage);
 * @endcode
 */
class FoliageMesh : public ProceduralMesh {
public:
    /**
     * @brief Plant type presets with tuned geometry and animation
     */
    enum class PlantType {
        Fern,       ///< Many small pinnae, moderate droop
        PalmFrond,  ///< Fewer large pinnae, heavy droop
        Grass,      ///< Simple tapered blade (no leaflets)
        Custom      ///< User-defined parameters
    };

    FoliageMesh();
    ~FoliageMesh() override;

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
    /// @name ProceduralMesh Interface
    /// @{

    /// Get the procedural frond mesh
    const Mesh& getMesh() const override { return m_frondMesh; }

    /// Get instance transforms and variations
    const std::vector<ProceduralInstance>& getInstances() const override { return m_instances; }

    /// Get wind animation parameters
    WindParams getWindParams() const override;

    /// Get base color for gradient
    glm::vec3 getBaseColor() const override {
        return glm::vec3(baseColor[0], baseColor[1], baseColor[2]);
    }

    /// Get tip color for gradient
    glm::vec3 getTipColor() const override {
        return glm::vec3(tipColor[0], tipColor[1], tipColor[2]);
    }

    /// @}
    // =========================================================================
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override {}
    std::string name() const override { return "FoliageMesh"; }

    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}

private:
    void generateFrondMesh();
    void generateInstances();
    void applyPreset(PlantType type);
    void checkForChanges();

    // Plant type
    PlantType m_plantType = PlantType::Fern;

    // Procedural mesh
    Mesh m_frondMesh;

    // Instance data
    std::vector<ProceduralInstance> m_instances;

    // Track geometry params for change detection
    float m_lastStemLength = 0;
    float m_lastStemCurve = 0;
    int m_lastLeafletPairs = 0;
    float m_lastLeafletWidth = 0;
    float m_lastLeafletLength = 0;
    float m_lastLeafletAngle = 0;

    // Track instance params for change detection
    int m_lastFrondCount = 0;
    int m_lastSeed = 0;
    float m_lastBaseHeight = 0;
    float m_lastFieldWidth = 0;
    float m_lastFieldDepth = 0;
    float m_lastSizeVariation = 0;

    bool m_initialized = false;
};

} // namespace vivid::render3d
