#pragma once

#include "vivid/operator.h"
#include "vivid/mesh.h"
#include "vivid/camera.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

// Forward declarations
namespace Diligent {
    class PBR_Renderer;
}

namespace vivid {

class PBRMaterial;
class IBLEnvironment;

/// A 3D object in the scene
struct Object3D {
    Mesh* mesh = nullptr;
    PBRMaterial* material = nullptr;  // Optional PBR material with textures
    glm::mat4 transform{1.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float uvScale = 1.0f;  // UV tiling scale
};

/// A light in the scene
struct Light3D {
    enum class Type { Directional, Point };

    Type type = Type::Directional;
    glm::vec3 position{0, 10, 0};      // Position for point lights
    glm::vec3 direction{-0.5f, -1.0f, -0.5f};  // Direction for directional lights
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

/// 3D rendering operator using DiligentFX PBR_Renderer
class Render3D : public Operator {
public:
    Render3D();
    ~Render3D() override;

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    std::string typeName() const override { return "Render3D"; }
    OutputKind outputKind() const override { return OutputKind::Texture; }

    Diligent::ITextureView* getOutputSRV() override;
    Diligent::ITextureView* getOutputRTV() override;

    // --- Scene Management ---

    /// Get the camera
    Camera3D& camera() { return camera_; }
    const Camera3D& camera() const { return camera_; }

    /// Add a mesh to the scene (returns object index)
    int addObject(Mesh* mesh, const glm::mat4& transform = glm::mat4(1.0f));

    /// Get object at index
    Object3D* getObject(int index);

    /// Clear all objects
    void clearObjects();

    /// Add a light (returns light index)
    int addLight(const Light3D& light);

    /// Get light at index
    Light3D* getLight(int index);

    /// Clear all lights
    void clearLights();

    // --- Scene Properties ---

    /// Set background color
    Render3D& backgroundColor(float r, float g, float b, float a = 1.0f);
    Render3D& backgroundColor(const glm::vec4& color);

    /// Set ambient light color
    Render3D& ambientColor(float r, float g, float b);
    Render3D& ambientColor(const glm::vec3& color);

    /// Set IBL environment for image-based lighting
    Render3D& setEnvironment(IBLEnvironment* env);

private:
    void createPipeline(Context& ctx);
    void createRenderTargets(Context& ctx);
    void renderScene(Context& ctx);

    // Scene data
    Camera3D camera_;
    std::vector<Object3D> objects_;
    std::vector<Light3D> lights_;
    glm::vec4 backgroundColor_{0.1f, 0.1f, 0.15f, 1.0f};
    glm::vec3 ambientColor_{0.1f, 0.1f, 0.1f};
    IBLEnvironment* environment_ = nullptr;

    // GPU resources - render targets
    Diligent::ITexture* colorTexture_ = nullptr;
    Diligent::ITextureView* colorRTV_ = nullptr;
    Diligent::ITextureView* colorSRV_ = nullptr;
    Diligent::ITexture* depthTexture_ = nullptr;
    Diligent::ITextureView* depthDSV_ = nullptr;

    // DiligentFX PBR Renderer
    std::unique_ptr<Diligent::PBR_Renderer> pbrRenderer_;
    Diligent::IShaderResourceBinding* srb_ = nullptr;

    // Constant buffers
    Diligent::IBuffer* frameAttribsBuffer_ = nullptr;
    Diligent::IBuffer* primitiveAttribsBuffer_ = nullptr;
    Diligent::IBuffer* materialAttribsBuffer_ = nullptr;

    int outputWidth_ = 0;
    int outputHeight_ = 0;
};

} // namespace vivid
