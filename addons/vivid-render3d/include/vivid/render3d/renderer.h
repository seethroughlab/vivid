#pragma once

#include <vivid/effects/texture_operator.h>
#include <vivid/render3d/mesh.h>
#include <vivid/render3d/camera.h>
#include <vivid/render3d/scene.h>
#include <vivid/render3d/shadow_manager.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>

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
struct LightData;
}

namespace vivid::render3d {

/// Shading mode for 3D rendering
enum class ShadingMode {
    Unlit,     ///< No lighting, just color/texture
    Flat,      ///< Per-fragment lighting (faceted look)
    Gouraud,   ///< Per-vertex lighting (smooth, PS1-style)
    VertexLit, ///< Simple per-vertex N·L diffuse with vertex colors
    Toon,      ///< Cel-shading with quantized lighting bands
    PBR        ///< Physically-based rendering with Cook-Torrance BRDF
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
    /// @deprecated Use Render3D::setInput(SceneComposer*) for chain visualizer integration
    [[deprecated("Use Render3D::setInput(SceneComposer*) for chain visualizer integration")]]
    void setScene(Scene& s);

    /// Set scene from a SceneComposer (node-based workflow)
    /// The composer's output scene will be rendered
    void setInput(SceneComposer* composer);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Camera
    /// @{

    /// Set camera from a CameraOperator (required for rendering)
    /// The operator's output camera will be used each frame
    void setCameraInput(CameraOperator* camOp);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Shading
    /// @{

    /// Set shading mode
    void setShadingMode(ShadingMode mode);

    /// Set default object color
    void setColor(float r, float g, float b, float a = 1.0f);
    void setColor(const glm::vec4& c);

    /// Set metallic factor for PBR (0 = dielectric, 1 = metal)
    void setMetallic(float m);

    /// Set roughness factor for PBR (0 = smooth/mirror, 1 = rough/diffuse)
    void setRoughness(float r);

    /// Set material with texture maps for PBR rendering
    /// When set, textures override scalar metallic/roughness values
    void setMaterial(TexturedMaterial* mat);

    /// Set number of toon shading bands (2-8, default 4)
    /// Only applies when shadingMode is Toon
    void setToonLevels(int levels);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Lighting
    /// @{

    /// Set light direction (normalized) - direct configuration
    void setLightDirection(glm::vec3 dir);

    /// Set light color - direct configuration
    void setLightColor(glm::vec3 color);

    /// Set ambient light level
    void setAmbient(float a);

    /// Set primary light from a LightOperator (node-based workflow)
    /// The operator's output light will be used each frame
    void setLightInput(LightOperator* lightOp);

    /// Add an additional light (node-based workflow, max 4 lights)
    void addLight(LightOperator* lightOp);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Image-Based Lighting (IBL)
    /// @{

    /// Enable/disable IBL for ambient lighting
    /// When enabled, environment maps provide realistic ambient light and reflections
    void setIbl(bool enabled);

    /// Set IBL environment from pre-baked cubemaps
    /// @param env Pointer to IBLEnvironment with loaded cubemaps
    /// @deprecated Use setEnvironmentInput() for operator-based workflow
    void setEnvironment(IBLEnvironment* env);

    /// Set IBL environment from an IBLEnvironment operator (node-based workflow)
    /// The operator's environment will be used each frame once loaded
    void setEnvironmentInput(IBLEnvironment* envOp);

    /// Load IBL environment from HDR file (generates cubemaps)
    /// @param hdrPath Path to .hdr environment map file
    /// @note This is slower than using pre-baked cubemaps but more convenient
    void setEnvironmentHDR(const std::string& hdrPath);

    /// Show environment map as skybox background
    /// Requires an IBL environment to be set
    void setShowSkybox(bool enabled);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Output
    /// @{

    /// Set output resolution (overrides TextureOperator::setResolution)
    void setResolution(int width, int height) {
        if (m_width != width || m_height != height) {
            m_width = width;
            m_height = height;
            markDirty();
        }
    }

    /// Set clear/background color
    void setClearColor(float r, float g, float b, float a = 1.0f);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Depth Output
    /// @{

    /// Enable linear depth output for post-processing (DOF, fog, etc.)
    /// When enabled, creates a second render target with linear depth (0=near, 1=far)
    void setDepthOutput(bool enabled);

    /// Get the linear depth texture view (nullptr if depth output disabled)
    /// Depth is normalized: 0.0 = near plane, 1.0 = far plane
    WGPUTextureView depthOutputView() const { return m_depthOutputView; }

    /// Check if depth output is enabled
    bool hasDepthOutput() const { return m_depthOutputEnabled; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Displacement
    /// @{

    /// Set displacement map from a texture operator (e.g., Noise)
    /// The red channel is used as displacement amount
    /// @param dispOp TextureOperator providing displacement map (nullptr to disable)
    void setDisplacementInput(effects::TextureOperator* dispOp);

    /// Set displacement amplitude (multiplier for displacement map values)
    /// @param amplitude Displacement strength (default 0.1)
    void setDisplacementAmplitude(float amplitude);

    /// Set displacement midpoint (value that produces zero displacement)
    /// Values below this displace inward, above displace outward
    /// @param midpoint Middle value (default 0.5 for 0-1 textures)
    void setDisplacementMidpoint(float midpoint);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Shadows
    /// @{

    /// Enable/disable shadow mapping
    /// @param enabled Whether to render shadows
    void setShadows(bool enabled);

    /// Set shadow map resolution (power of 2: 512, 1024, 2048)
    /// Higher resolution = sharper shadows but more memory/performance cost
    void setShadowMapResolution(int size);

    /// Check if shadows are enabled
    bool hasShadows() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Debug
    /// @{

    /// Enable wireframe rendering
    void setWireframe(bool enabled);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Render3D"; }
    // outputKind() and outputView() inherited from TextureOperator

    std::vector<ParamDecl> params() override {
        return {
            {"metallic", ParamType::Float, 0.0f, 1.0f, {m_metallic, 0, 0, 0}},
            {"roughness", ParamType::Float, 0.0f, 1.0f, {m_roughness, 0, 0, 0}},
            {"ambient", ParamType::Float, 0.0f, 1.0f, {m_ambient, 0, 0, 0}},
            {"lightDir", ParamType::Vec3, -1.0f, 1.0f,
             {m_lightDirection.x, m_lightDirection.y, m_lightDirection.z, 0}},
            {"lightColor", ParamType::Vec3, 0.0f, 2.0f,
             {m_lightColor.x, m_lightColor.y, m_lightColor.z, 0}},
            {"clearColor", ParamType::Vec4, 0.0f, 1.0f,
             {m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a}},
            {"toonLevels", ParamType::Float, 2.0f, 8.0f, {static_cast<float>(m_toonLevels), 0, 0, 0}},
            {"wireframe", ParamType::Float, 0.0f, 1.0f, {m_wireframe ? 1.0f : 0.0f, 0, 0, 0}}
        };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "metallic") { out[0] = m_metallic; return true; }
        if (name == "roughness") { out[0] = m_roughness; return true; }
        if (name == "ambient") { out[0] = m_ambient; return true; }
        if (name == "lightDir") {
            out[0] = m_lightDirection.x; out[1] = m_lightDirection.y; out[2] = m_lightDirection.z;
            return true;
        }
        if (name == "lightColor") {
            out[0] = m_lightColor.x; out[1] = m_lightColor.y; out[2] = m_lightColor.z;
            return true;
        }
        if (name == "clearColor") {
            out[0] = m_clearColor.r; out[1] = m_clearColor.g; out[2] = m_clearColor.b; out[3] = m_clearColor.a;
            return true;
        }
        if (name == "toonLevels") { out[0] = static_cast<float>(m_toonLevels); return true; }
        if (name == "wireframe") { out[0] = m_wireframe ? 1.0f : 0.0f; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "metallic") { m_metallic = value[0]; markDirty(); return true; }
        if (name == "roughness") { m_roughness = value[0]; markDirty(); return true; }
        if (name == "ambient") { m_ambient = value[0]; markDirty(); return true; }
        if (name == "lightDir") {
            m_lightDirection = glm::normalize(glm::vec3(value[0], value[1], value[2]));
            markDirty();
            return true;
        }
        if (name == "lightColor") {
            m_lightColor = glm::vec3(value[0], value[1], value[2]);
            markDirty();
            return true;
        }
        if (name == "clearColor") {
            m_clearColor = glm::vec4(value[0], value[1], value[2], value[3]);
            markDirty();
            return true;
        }
        if (name == "toonLevels") { m_toonLevels = static_cast<int>(value[0]); markDirty(); return true; }
        if (name == "wireframe") { m_wireframe = value[0] > 0.5f; markDirty(); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);
    void createDepthBuffer(Context& ctx);

    // Pipeline creation helpers
    WGPUShaderModule createShaderModule(WGPUDevice device, const std::string& source, const char* label);
    void initVertexLayout();  // Initialize m_vertexAttrs and m_vertexLayout
    WGPUDepthStencilState getStandardDepthStencil();

    // Debug visualization helpers
    void renderDebugVisualization(Context& ctx, WGPURenderPassEncoder pass);
    bool hasShadowCastingLight() const;
    bool hasPointLightShadow() const;

    // Scene
    Scene* m_scene = nullptr;
    SceneComposer* m_composer = nullptr;  // Alternative to m_scene for node-based workflow
    CameraOperator* m_cameraOp = nullptr;  // Required for rendering

    // Light operators (node-based workflow)
    std::vector<LightOperator*> m_lightOps;

    // Shading
    ShadingMode m_shadingMode = ShadingMode::Flat;
    glm::vec4 m_defaultColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    glm::vec3 m_lightDirection = glm::normalize(glm::vec3(1, 1, 1));
    glm::vec3 m_lightColor = glm::vec3(1, 1, 1);
    float m_ambient = 0.1f;
    int m_toonLevels = 4;  // Toon shading bands (2-8)

    // PBR parameters
    float m_metallic = 0.0f;
    float m_roughness = 0.5f;
    TexturedMaterial* m_material = nullptr;  // Optional textured material

    // IBL parameters
    bool m_iblEnabled = false;
    bool m_showSkybox = false;
    IBLEnvironment* m_iblEnvironment = nullptr;
    IBLEnvironment* m_iblEnvironmentOp = nullptr;  // Operator-based input
    std::string m_pendingHDRPath;  // For deferred HDR loading

    // Output - m_output, m_outputView, m_width, m_height inherited from TextureOperator
    glm::vec4 m_clearColor = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f);

    // Debug
    bool m_wireframe = false;

    // Shadow mapping (delegated to ShadowManager)
    std::unique_ptr<ShadowManager> m_shadowManager;

    // Displacement
    effects::TextureOperator* m_displacementOp = nullptr;
    float m_displacementAmplitude = 0.1f;
    float m_displacementMidpoint = 0.5f;

    // Depth output for post-processing
    bool m_depthOutputEnabled = false;
    WGPUTexture m_depthOutputTexture = nullptr;
    WGPUTextureView m_depthOutputView = nullptr;
    WGPURenderPipeline m_depthCopyPipeline = nullptr;
    WGPUBindGroupLayout m_depthCopyBindGroupLayout = nullptr;
    WGPUBindGroup m_depthCopyBindGroup = nullptr;
    WGPUSampler m_depthCopySampler = nullptr;
    WGPUBuffer m_depthCopyUniformBuffer = nullptr;

    // GPU resources (depth buffer is 3D-specific, not in TextureOperator)
    WGPUTexture m_depthTexture = nullptr;
    WGPUTextureView m_depthView = nullptr;
    int m_depthWidth = 0;   // Track depth buffer size for resize detection
    int m_depthHeight = 0;

    // Shared vertex layout (initialized once in createPipeline)
    WGPUVertexAttribute m_vertexAttrs[5] = {};
    WGPUVertexBufferLayout m_vertexLayout = {};

    WGPURenderPipeline m_pipeline = nullptr;           // Flat/Gouraud/Unlit
    WGPURenderPipeline m_pbrPipeline = nullptr;        // PBR with scalar values
    WGPURenderPipeline m_pbrTexturedPipeline = nullptr; // PBR with texture maps
    WGPURenderPipeline m_pbrTexturedBlendPipeline = nullptr;     // PBR textured with alpha blending
    WGPURenderPipeline m_pbrTexturedDoubleSidedPipeline = nullptr; // PBR textured, double-sided
    WGPURenderPipeline m_pbrTexturedBlendDoubleSidedPipeline = nullptr; // Both
    WGPURenderPipeline m_pbrIBLPipeline = nullptr;       // PBR with IBL
    WGPURenderPipeline m_pbrIBLBlendPipeline = nullptr;  // PBR IBL with alpha blending
    WGPURenderPipeline m_pbrIBLDoubleSidedPipeline = nullptr; // PBR IBL, double-sided
    WGPURenderPipeline m_pbrIBLBlendDoubleSidedPipeline = nullptr; // Both
    WGPURenderPipeline m_wireframePipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBindGroupLayout m_pbrBindGroupLayout = nullptr;
    WGPUBindGroupLayout m_pbrTexturedBindGroupLayout = nullptr;
    WGPUBindGroupLayout m_iblBindGroupLayout = nullptr;  // IBL textures (group 3)
    WGPUBindGroup m_iblBindGroup = nullptr;              // IBL bind group
    WGPUSampler m_iblSampler = nullptr;                  // Sampler for IBL cubemaps
    WGPURenderPipeline m_skyboxPipeline = nullptr;       // Skybox rendering
    WGPUBindGroupLayout m_skyboxBindGroupLayout = nullptr;
    WGPUBindGroup m_skyboxBindGroup = nullptr;
    WGPUBuffer m_skyboxUniformBuffer = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUBuffer m_pbrUniformBuffer = nullptr;

    // Displacement GPU resources
    WGPURenderPipeline m_pbrDisplacementPipeline = nullptr;
    WGPUBindGroupLayout m_displacementBindGroupLayout = nullptr;  // Displacement texture (group 4)
    WGPUBindGroup m_displacementBindGroup = nullptr;
    WGPUSampler m_displacementSampler = nullptr;
    WGPUBuffer m_displacementUniformBuffer = nullptr;
    WGPUBindGroup m_flatBindGroup = nullptr;      // Cached bind group for flat/gouraud shading
    WGPUBindGroup m_scalarPbrBindGroup = nullptr; // Cached bind group for scalar PBR
    std::vector<WGPUBindGroup> m_bindGroups;  // One per object
    size_t m_uniformAlignment = 256;  // WebGPU minimum uniform buffer alignment
    size_t m_pbrUniformAlignment = 256;
    static constexpr size_t MAX_OBJECTS = 256;
};

} // namespace vivid::render3d
