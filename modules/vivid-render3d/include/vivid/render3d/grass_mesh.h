#pragma once

/**
 * @file grass_mesh.h
 * @brief Procedural grass blade mesh generator
 *
 * Generates grass blade geometry for GPU instancing.
 * Used with Render3D for unified rendering with shadows and lighting.
 */

#include <vivid/render3d/procedural_mesh.h>
#include <vivid/param.h>
#include <glm/glm.hpp>
#include <vector>

namespace vivid::render3d {

/**
 * @brief Procedural grass blade mesh generator
 *
 * Generates tapered grass blade geometry for GPU instancing.
 * This is a pure mesh generator - rendering is handled by Render3D
 * for unified shadows and lighting.
 *
 * @par Example
 * @code
 * auto& camera = chain.add<CameraOperator>("camera");
 * auto& sun = chain.add<DirectionalLight>("sun");
 *
 * auto& grass = chain.add<GrassMesh>("grass");
 * grass.bladeCount = 10000;
 * grass.fieldWidth = 20.0f;
 *
 * auto& render = chain.add<Render3D>("render");
 * render.setCameraInput(&camera);
 * render.setLightInput(&sun);
 * render.addProceduralMesh(&grass);
 * @endcode
 */
class GrassMesh : public ProceduralMesh {
public:
    GrassMesh();
    ~GrassMesh() override;

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
    /// @name ProceduralMesh Interface
    /// @{

    /// Get the procedural blade mesh
    const Mesh& getMesh() const override { return m_bladeMesh; }

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
    std::string name() const override { return "GrassMesh"; }

    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}

private:
    void createBladeMesh();
    void generateInstances();

    // Blade mesh
    Mesh m_bladeMesh;

    // Instance data
    std::vector<ProceduralInstance> m_instances;

    // Track params for change detection
    int m_lastBladeCount = 0;
    int m_lastSeed = 0;
    float m_lastFieldWidth = 0;
    float m_lastFieldDepth = 0;
    float m_lastBladeHeight = 0;
    float m_lastHeightVariation = 0;

    bool m_initialized = false;
};

} // namespace vivid::render3d
