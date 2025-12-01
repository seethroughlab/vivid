#pragma once
#include "renderer.h"
#include "pipeline3d.h"
#include "mesh.h"
#include <vivid/graphics3d.h>
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace vivid {

// Maximum number of lights in a single draw call
constexpr int MAX_SHADER_LIGHTS = 8;

/**
 * @brief GPU-compatible light data structure.
 * Must match the WGSL struct layout exactly.
 */
struct LightUniform {
    int32_t lightType;        // 0=directional, 1=point, 2=spot
    float _pad1[3];           // Padding to align next vec3

    glm::vec3 position;
    float _pad2;

    glm::vec3 direction;
    float _pad3;

    glm::vec3 color;
    float intensity;

    float radius;
    float innerAngle;
    float outerAngle;
    float _pad4;
};

/**
 * @brief GPU-compatible lighting uniform buffer.
 */
struct LightsUniform {
    LightUniform lights[MAX_SHADER_LIGHTS];
    int32_t lightCount;
    float _pad1[3];

    glm::vec3 ambientColor;
    float ambientIntensity;
};

/**
 * @brief GPU-compatible Phong material uniform.
 */
struct PhongMaterialUniform {
    glm::vec3 ambient;
    float _pad1;
    glm::vec3 diffuse;
    float _pad2;
    glm::vec3 specular;
    float shininess;
    glm::vec3 emissive;
    float _pad3;
};

/**
 * @brief GPU-compatible PBR material uniform.
 * Must be 64 bytes to match PhongMaterialUniform for shared bind group layout.
 */
struct PBRMaterialUniform {
    glm::vec3 albedo;
    float _pad0;
    float metallic;
    float roughness;
    float ao;
    float _pad1;
    glm::vec3 emissive;
    float _pad2;
    float _pad3[4];  // Pad to 64 bytes to match Phong material size
};

// Helper to convert CPU Light to GPU LightUniform
inline LightUniform makeLightUniform(const Light& light) {
    LightUniform u = {};
    u.lightType = static_cast<int32_t>(light.type);
    u.position = light.position;
    u.direction = light.direction;
    u.color = light.color;
    u.intensity = light.intensity;
    u.radius = light.radius;
    u.innerAngle = light.innerAngle;
    u.outerAngle = light.outerAngle;
    return u;
}

// Helper to convert SceneLighting to GPU LightsUniform
inline LightsUniform makeLightsUniform(const SceneLighting& scene) {
    LightsUniform u = {};
    u.lightCount = static_cast<int32_t>(std::min(scene.lights.size(), static_cast<size_t>(MAX_SHADER_LIGHTS)));
    u.ambientColor = scene.ambientColor;
    u.ambientIntensity = scene.ambientIntensity;

    for (int i = 0; i < u.lightCount; ++i) {
        u.lights[i] = makeLightUniform(scene.lights[i]);
    }
    return u;
}

// Helper to convert PhongMaterial to GPU uniform
inline PhongMaterialUniform makePhongMaterialUniform(const PhongMaterial& mat) {
    PhongMaterialUniform u = {};
    u.ambient = mat.ambient;
    u.diffuse = mat.diffuse;
    u.specular = mat.specular;
    u.shininess = mat.shininess;
    u.emissive = mat.emissive;
    return u;
}

// Helper to convert PBRMaterial to GPU uniform
inline PBRMaterialUniform makePBRMaterialUniform(const PBRMaterial& mat) {
    PBRMaterialUniform u = {};
    u.albedo = mat.albedo;
    u.metallic = mat.metallic;
    u.roughness = mat.roughness;
    u.ao = mat.ao;
    u.emissive = mat.emissive;
    return u;
}

/**
 * @brief GPU-compatible textured PBR material uniform.
 * Contains base values and flags for which textures are present.
 */
struct TexturedPBRMaterialUniform {
    glm::vec3 albedo;
    float metallic;
    float roughness;
    float ao;
    float normalStrength;
    float emissiveStrength;
    glm::vec3 emissive;
    uint32_t textureFlags;  // Bit flags: 1=albedo, 2=normal, 4=metallicRoughness, 8=ao, 16=emissive
};

// Helper to convert TexturedPBRMaterial to GPU uniform
inline TexturedPBRMaterialUniform makeTexturedPBRMaterialUniform(const TexturedPBRMaterial& mat) {
    TexturedPBRMaterialUniform u = {};
    u.albedo = mat.albedo;
    u.metallic = mat.metallic;
    u.roughness = mat.roughness;
    u.ao = mat.ao;
    u.normalStrength = mat.normalStrength;
    u.emissiveStrength = mat.emissiveStrength;
    u.emissive = mat.emissive;

    // Set texture flags based on which textures are present
    u.textureFlags = 0;
    if (mat.albedoMap) u.textureFlags |= 1;
    if (mat.normalMap) u.textureFlags |= 2;
    if (mat.metallicRoughnessMap) u.textureFlags |= 4;
    if (mat.aoMap) u.textureFlags |= 8;
    if (mat.emissiveMap) u.textureFlags |= 16;
    if (mat.roughnessMap) u.textureFlags |= 32;
    if (mat.metallicMap) u.textureFlags |= 64;

    return u;
}

/**
 * @brief Stencil modes for lit 3D rendering.
 *
 * Used to enable different stencil buffer operations.
 */
enum class StencilMode {
    None,           ///< No stencil operations (default)
    Write,          ///< Write stencil reference value where rendered
    TestEqual,      ///< Only render where stencil == reference
    TestNotEqual    ///< Only render where stencil != reference
};

/**
 * @brief Lit 3D rendering pipeline supporting Phong and PBR shading.
 *
 * Bind groups:
 * - Group 0: Camera uniform
 * - Group 1: Transform uniform
 * - Group 2: Lights uniform
 * - Group 3: Material uniform (Phong or PBR)
 */
class Pipeline3DLit {
public:
    enum class ShadingModel {
        Phong,
        PBR,
        PBR_IBL,
        PBR_IBL_Textured  // With texture maps support
    };

    Pipeline3DLit() = default;
    ~Pipeline3DLit();

    // Non-copyable
    Pipeline3DLit(const Pipeline3DLit&) = delete;
    Pipeline3DLit& operator=(const Pipeline3DLit&) = delete;

    /**
     * @brief Initialize the lit pipeline.
     * @param renderer The renderer for GPU resources.
     * @param model The shading model to use.
     * @return true if successful.
     */
    bool init(Renderer& renderer, ShadingModel model);

    /**
     * @brief Destroy GPU resources.
     */
    void destroy();

    /**
     * @brief Check if pipeline is valid.
     */
    bool valid() const { return pipeline_ != nullptr; }

    /**
     * @brief Render a mesh with Phong shading.
     */
    void renderPhong(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                     const PhongMaterial& material, const SceneLighting& lighting,
                     Texture& output, const glm::vec4& clearColor = {0, 0, 0, 1});

    /**
     * @brief Render a mesh with PBR shading.
     */
    void renderPBR(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                   const PBRMaterial& material, const SceneLighting& lighting,
                   Texture& output, const glm::vec4& clearColor = {0, 0, 0, 1});

    /**
     * @brief Render a mesh with PBR shading and Image-Based Lighting.
     * @param env The IBL environment (irradiance, radiance, BRDF LUT)
     */
    void renderPBRWithIBL(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                          const PBRMaterial& material, const SceneLighting& lighting,
                          const Environment& env,
                          Texture& output, const glm::vec4& clearColor = {0, 0, 0, 1});

    /**
     * @brief Render a mesh with textured PBR shading and IBL.
     * @param material The textured PBR material with optional texture maps.
     * @param env The IBL environment (irradiance, radiance, BRDF LUT)
     */
    void renderPBRTexturedWithIBL(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                                   const TexturedPBRMaterial& material, const SceneLighting& lighting,
                                   const Environment& env,
                                   Texture& output, const glm::vec4& clearColor = {0, 0, 0, 1});

    // ========================================================================
    // Stencil-enabled rendering methods
    // ========================================================================

    /**
     * @brief Render with PBR shading and stencil operations.
     * @param stencilMode The stencil operation mode.
     * @param stencilRef The stencil reference value (0-255).
     */
    void renderPBRWithStencil(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                              const PBRMaterial& material, const SceneLighting& lighting,
                              StencilMode stencilMode, uint8_t stencilRef,
                              Texture& output, const glm::vec4& clearColor = {0, 0, 0, 1});

    /**
     * @brief Render with Phong shading and stencil operations.
     * @param stencilMode The stencil operation mode.
     * @param stencilRef The stencil reference value (0-255).
     */
    void renderPhongWithStencil(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                                const PhongMaterial& material, const SceneLighting& lighting,
                                StencilMode stencilMode, uint8_t stencilRef,
                                Texture& output, const glm::vec4& clearColor = {0, 0, 0, 1});

    /**
     * @brief Get the shading model.
     */
    ShadingModel shadingModel() const { return model_; }

private:
    bool createPipeline(const std::string& shaderSource);
    bool createStencilPipeline(const std::string& shaderSource, StencilMode mode);
    void beginRenderPass(Texture& output, const glm::vec4& clearColor);
    void endRenderPass();
    WGPURenderPipeline getStencilPipeline(StencilMode mode);

    Renderer* renderer_ = nullptr;
    ShadingModel model_ = ShadingModel::Phong;

    WGPURenderPipeline pipeline_ = nullptr;  // StencilMode::None

    // Stencil pipeline variants (indexed by StencilMode)
    static constexpr int STENCIL_MODE_COUNT = 4;
    WGPURenderPipeline stencilPipelines_[STENCIL_MODE_COUNT] = {nullptr, nullptr, nullptr, nullptr};
    uint8_t stencilRef_ = 0;  // Current stencil reference for render pass
    WGPUBindGroupLayout cameraLayout_ = nullptr;
    WGPUBindGroupLayout transformLayout_ = nullptr;
    WGPUBindGroupLayout lightsLayout_ = nullptr;
    WGPUBindGroupLayout materialLayout_ = nullptr;  // Combined with IBL for PBR_IBL mode
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUSampler iblSampler_ = nullptr;      // Filtering sampler for IBL cubemaps
    WGPUSampler brdfSampler_ = nullptr;     // Non-filtering sampler for BRDF LUT
    WGPUSampler textureSampler_ = nullptr;  // Filtering sampler for material textures

    // Reusable buffers
    WGPUBuffer cameraBuffer_ = nullptr;
    WGPUBuffer transformBuffer_ = nullptr;
    WGPUBuffer lightsBuffer_ = nullptr;
    WGPUBuffer materialBuffer_ = nullptr;

    // Depth buffer
    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
    int depthWidth_ = 0;
    int depthHeight_ = 0;

    // Render pass state
    WGPUCommandEncoder encoder_ = nullptr;
    WGPURenderPassEncoder renderPass_ = nullptr;

    void ensureDepthBuffer(int width, int height);
    void destroyDepthBuffer();
};

// Built-in lit shaders
namespace shaders3d {

extern const char* PHONG_LIT;
extern const char* PBR_LIT;
extern const char* PBR_IBL;
extern const char* PBR_IBL_TEXTURED;

} // namespace shaders3d

} // namespace vivid
