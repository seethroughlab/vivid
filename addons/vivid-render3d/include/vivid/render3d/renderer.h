#pragma once

#include <vivid/effects/texture_operator.h>
#include <vivid/render3d/mesh.h>
#include <vivid/render3d/camera.h>
#include <vivid/render3d/scene.h>
#include <glm/glm.hpp>
#include <string>

namespace vivid {
class Context;
}

namespace vivid::render3d {
// Forward declarations
class SceneComposer;
class CameraOperator;
class LightOperator;
class TexturedMaterial;
class IBLEnvironment;
}

namespace vivid::render3d {

/// Shading mode for 3D rendering
enum class ShadingMode {
    Unlit,    ///< No lighting, just color/texture
    Flat,     ///< Per-fragment lighting (faceted look)
    Gouraud,  ///< Per-vertex lighting (smooth, PS1-style)
    PBR       ///< Physically-based rendering with Cook-Torrance BRDF
};

/// 3D renderer operator - extends TextureOperator for consistent architecture
class Render3D : public effects::TextureOperator {
public:
    Render3D();
    ~Render3D();

    // -------------------------------------------------------------------------
    /// @name Scene Setup
    /// @{

    /// Set the scene to render (manual scene management)
    /// @deprecated Use Render3D::input(SceneComposer*) for chain visualizer integration
    [[deprecated("Use Render3D::input(SceneComposer*) for chain visualizer integration")]]
    Render3D& scene(Scene& s);

    /// Set scene from a SceneComposer (node-based workflow)
    /// The composer's output scene will be rendered
    Render3D& input(SceneComposer* composer);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Camera
    /// @{

    /// Set the camera (direct configuration)
    Render3D& camera(const Camera3D& cam);

    /// Set camera from a CameraOperator (node-based workflow)
    /// The operator's output camera will be used each frame
    Render3D& cameraInput(CameraOperator* camOp);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Shading
    /// @{

    /// Set shading mode
    Render3D& shadingMode(ShadingMode mode);

    /// Set default object color
    Render3D& color(float r, float g, float b, float a = 1.0f);
    Render3D& color(const glm::vec4& c);

    /// Set metallic factor for PBR (0 = dielectric, 1 = metal)
    Render3D& metallic(float m);

    /// Set roughness factor for PBR (0 = smooth/mirror, 1 = rough/diffuse)
    Render3D& roughness(float r);

    /// Set material with texture maps for PBR rendering
    /// When set, textures override scalar metallic/roughness values
    Render3D& material(TexturedMaterial* mat);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Lighting
    /// @{

    /// Set light direction (normalized) - direct configuration
    Render3D& lightDirection(glm::vec3 dir);

    /// Set light color - direct configuration
    Render3D& lightColor(glm::vec3 color);

    /// Set ambient light level
    Render3D& ambient(float a);

    /// Set primary light from a LightOperator (node-based workflow)
    /// The operator's output light will be used each frame
    Render3D& lightInput(LightOperator* lightOp);

    /// Add an additional light (node-based workflow, max 4 lights)
    Render3D& addLight(LightOperator* lightOp);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Image-Based Lighting (IBL)
    /// @{

    /// Enable/disable IBL for ambient lighting
    /// When enabled, environment maps provide realistic ambient light and reflections
    Render3D& ibl(bool enabled);

    /// Set IBL environment from pre-baked cubemaps
    /// @param env Pointer to IBLEnvironment with loaded cubemaps
    Render3D& environment(IBLEnvironment* env);

    /// Load IBL environment from HDR file (generates cubemaps)
    /// @param hdrPath Path to .hdr environment map file
    /// @note This is slower than using pre-baked cubemaps but more convenient
    Render3D& environmentHDR(const std::string& hdrPath);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Output
    /// @{

    /// Set output resolution (overrides TextureOperator::resolution)
    Render3D& resolution(int width, int height) {
        m_width = width;
        m_height = height;
        return *this;
    }

    /// Set clear/background color
    Render3D& clearColor(float r, float g, float b, float a = 1.0f);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Debug
    /// @{

    /// Enable wireframe rendering
    Render3D& wireframe(bool enabled);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Render3D"; }
    // outputKind() and outputView() inherited from TextureOperator

    /// @}

private:
    void createPipeline(Context& ctx);
    void createDepthBuffer(Context& ctx);

    // Scene
    Scene* m_scene = nullptr;
    SceneComposer* m_composer = nullptr;  // Alternative to m_scene for node-based workflow
    Camera3D m_camera;
    CameraOperator* m_cameraOp = nullptr;  // Alternative to m_camera for node-based workflow

    // Light operators (node-based workflow)
    std::vector<LightOperator*> m_lightOps;

    // Shading
    ShadingMode m_shadingMode = ShadingMode::Flat;
    glm::vec4 m_defaultColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    glm::vec3 m_lightDirection = glm::normalize(glm::vec3(1, 1, 1));
    glm::vec3 m_lightColor = glm::vec3(1, 1, 1);
    float m_ambient = 0.1f;

    // PBR parameters
    float m_metallic = 0.0f;
    float m_roughness = 0.5f;
    TexturedMaterial* m_material = nullptr;  // Optional textured material

    // IBL parameters
    bool m_iblEnabled = false;
    IBLEnvironment* m_iblEnvironment = nullptr;
    std::string m_pendingHDRPath;  // For deferred HDR loading

    // Output - m_output, m_outputView, m_width, m_height inherited from TextureOperator
    glm::vec4 m_clearColor = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f);

    // Debug
    bool m_wireframe = false;

    // GPU resources (depth buffer is 3D-specific, not in TextureOperator)
    WGPUTexture m_depthTexture = nullptr;
    WGPUTextureView m_depthView = nullptr;
    WGPURenderPipeline m_pipeline = nullptr;           // Flat/Gouraud/Unlit
    WGPURenderPipeline m_pbrPipeline = nullptr;        // PBR with scalar values
    WGPURenderPipeline m_pbrTexturedPipeline = nullptr; // PBR with texture maps
    WGPURenderPipeline m_pbrIBLPipeline = nullptr;       // PBR with IBL
    WGPURenderPipeline m_wireframePipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBindGroupLayout m_pbrBindGroupLayout = nullptr;
    WGPUBindGroupLayout m_pbrTexturedBindGroupLayout = nullptr;
    WGPUBindGroupLayout m_iblBindGroupLayout = nullptr;  // IBL textures (group 3)
    WGPUBindGroup m_iblBindGroup = nullptr;              // IBL bind group
    WGPUSampler m_iblSampler = nullptr;                  // Sampler for IBL cubemaps
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUBuffer m_pbrUniformBuffer = nullptr;
    std::vector<WGPUBindGroup> m_bindGroups;  // One per object
    size_t m_uniformAlignment = 256;  // WebGPU minimum uniform buffer alignment
    size_t m_pbrUniformAlignment = 256;
    static constexpr size_t MAX_OBJECTS = 256;

    bool m_initialized = false;
};

} // namespace vivid::render3d
