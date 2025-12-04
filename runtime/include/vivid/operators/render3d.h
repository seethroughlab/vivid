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
    enum class Type { Directional, Point, Spot };

    Type type = Type::Directional;
    glm::vec3 position{0, 10, 0};      // Position for point/spot lights
    glm::vec3 direction{-0.5f, -1.0f, -0.5f};  // Direction for directional/spot lights
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;               // Range for point/spot lights (0 = infinite)
    float innerConeAngle = 0.0f;       // Spot light inner cone (radians)
    float outerConeAngle = 0.7854f;    // Spot light outer cone (radians, ~45 degrees)

    // Factory methods for convenience
    static Light3D directional(const glm::vec3& dir, float intensity = 3.0f,
                               const glm::vec3& color = glm::vec3(1.0f)) {
        Light3D l;
        l.type = Type::Directional;
        l.direction = glm::normalize(dir);
        l.intensity = intensity;
        l.color = color;
        return l;
    }

    static Light3D point(const glm::vec3& pos, float intensity = 100.0f,
                         float range = 10.0f, const glm::vec3& color = glm::vec3(1.0f)) {
        Light3D l;
        l.type = Type::Point;
        l.position = pos;
        l.intensity = intensity;
        l.range = range;
        l.color = color;
        return l;
    }

    static Light3D spot(const glm::vec3& pos, const glm::vec3& dir,
                        float intensity = 200.0f, float innerAngle = 0.2f,
                        float outerAngle = 0.5f, float range = 15.0f,
                        const glm::vec3& color = glm::vec3(1.0f)) {
        Light3D l;
        l.type = Type::Spot;
        l.position = pos;
        l.direction = glm::normalize(dir);
        l.intensity = intensity;
        l.innerConeAngle = innerAngle;
        l.outerConeAngle = outerAngle;
        l.range = range;
        l.color = color;
        return l;
    }
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

    /// Add a light (returns light index, -1 if max lights reached)
    int addLight(const Light3D& light);

    /// Get light at index
    Light3D* getLight(int index);

    /// Set light at index
    void setLight(int index, const Light3D& light);

    /// Get number of lights
    int lightCount() const { return static_cast<int>(lights_.size()); }

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
