#pragma once

#include "vivid/operator.h"
#include "vivid/export.h"
#include "vivid/mesh.h"
#include "vivid/camera.h"
#include <glm/glm.hpp>
#include <vector>

namespace Diligent {
    struct IBuffer;
    struct IPipelineState;
    struct IShaderResourceBinding;
    struct IRenderDevice;
    struct IDeviceContext;
    struct ISampler;
}

namespace vivid {

class PBRMaterial;
class IBLEnvironment;

/// Per-instance data for GPU instancing
struct VIVID_API Instance3D {
    glm::mat4 transform{1.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float materialIndex{0.0f};  // Index into texture array (for PBR materials)
    float metallic{0.0f};       // Override metallic (0-1), used when no material
    float roughness{0.5f};      // Override roughness (0-1), used when no material
    float _padding{0.0f};       // Padding for alignment

    Instance3D() = default;
    Instance3D(const glm::mat4& t, const glm::vec4& c) : transform(t), color(c) {}
    Instance3D(const glm::mat4& t, const glm::vec4& c, float matIdx)
        : transform(t), color(c), materialIndex(matIdx) {}
    Instance3D(const glm::mat4& t, const glm::vec4& c, float metal, float rough)
        : transform(t), color(c), metallic(metal), roughness(rough) {}
};

/// A directional light for instanced rendering
struct VIVID_API InstancedLight {
    glm::vec3 direction{-0.5f, -1.0f, -0.5f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{1.0f};

    InstancedLight() = default;
    InstancedLight(const glm::vec3& dir, float i = 1.0f, const glm::vec3& col = glm::vec3(1.0f))
        : direction(glm::normalize(dir)), color(col), intensity(i) {}
};

/// GPU-instanced 3D rendering operator
/// Renders thousands of instances of a mesh in a single draw call
class VIVID_API InstancedRender3D : public Operator {
public:
    InstancedRender3D();
    ~InstancedRender3D() override;

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    std::string typeName() const override { return "InstancedRender3D"; }
    OutputKind outputKind() const override { return OutputKind::Texture; }

    Diligent::ITextureView* getOutputSRV() override;
    Diligent::ITextureView* getOutputRTV() override;

    // --- Scene Management ---

    /// Get/set the camera
    Camera3D& camera() { return camera_; }
    const Camera3D& camera() const { return camera_; }

    /// Set the mesh to instance
    InstancedRender3D& setMesh(Mesh* mesh);

    /// Set all instances at once (uploads to GPU)
    InstancedRender3D& setInstances(const std::vector<Instance3D>& instances);

    /// Clear all instances
    InstancedRender3D& clearInstances();

    /// Get number of instances
    size_t instanceCount() const { return instanceCount_; }

    // --- Scene Properties ---

    /// Set background color
    InstancedRender3D& backgroundColor(float r, float g, float b, float a = 1.0f);
    InstancedRender3D& backgroundColor(const glm::vec4& color);

    /// Set ambient light color
    InstancedRender3D& ambientColor(float r, float g, float b);
    InstancedRender3D& ambientColor(const glm::vec3& color);

    /// Set directional light
    InstancedRender3D& setLight(const InstancedLight& light);

    /// Set PBR material (optional - uses per-instance color/metallic/roughness if not set)
    InstancedRender3D& setMaterial(PBRMaterial* material);

    /// Set UV scale for texture tiling
    InstancedRender3D& uvScale(float scale) { uvScale_ = scale; return *this; }

    /// Set IBL environment for image-based lighting
    InstancedRender3D& setEnvironment(IBLEnvironment* env);

    /// Set IBL intensity scale
    InstancedRender3D& iblScale(float scale) { iblScale_ = scale; return *this; }

private:
    void createPipeline(Context& ctx);
    void createRenderTargets(Context& ctx);
    void renderScene(Context& ctx);

    // Scene data
    Camera3D camera_;
    Mesh* mesh_ = nullptr;
    PBRMaterial* material_ = nullptr;
    IBLEnvironment* environment_ = nullptr;
    InstancedLight light_;
    glm::vec4 backgroundColor_{0.02f, 0.02f, 0.05f, 1.0f};
    glm::vec3 ambientColor_{0.1f, 0.1f, 0.15f};
    float uvScale_ = 1.0f;
    float iblScale_ = 1.0f;

    // Instance data
    size_t instanceCount_ = 0;
    size_t instanceBufferCapacity_ = 0;
    Diligent::IBuffer* instanceBuffer_ = nullptr;

    // GPU resources - render targets
    Diligent::ITexture* colorTexture_ = nullptr;
    Diligent::ITextureView* colorRTV_ = nullptr;
    Diligent::ITextureView* colorSRV_ = nullptr;
    Diligent::ITexture* depthTexture_ = nullptr;
    Diligent::ITextureView* depthDSV_ = nullptr;

    // Pipeline
    Diligent::IPipelineState* pso_ = nullptr;
    Diligent::IShaderResourceBinding* srb_ = nullptr;
    Diligent::IBuffer* frameConstantsBuffer_ = nullptr;

    // Cached device/context for instance buffer updates
    Diligent::IRenderDevice* device_ = nullptr;
    Diligent::IDeviceContext* context_ = nullptr;

    int outputWidth_ = 0;
    int outputHeight_ = 0;
};

} // namespace vivid
