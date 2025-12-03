#pragma once

// DiligentPBR - PBR rendering wrapper using DiligentFX
// Provides proper PBR with shadows using Diligent's battle-tested implementation

#ifdef VIVID_USE_DILIGENT

#include <vivid/graphics3d.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

// Forward declarations for Diligent types
namespace Diligent {
    struct IRenderDevice;
    struct IDeviceContext;
    struct ISwapChain;
    struct IBuffer;
    struct ITexture;
    struct ITextureView;
    struct IPipelineState;
    struct IShaderResourceBinding;
    class ShadowMapManager;
    class GLTF_PBR_Renderer;
}

namespace vivid {

// Forward declarations
class DiligentRenderer;
struct Texture;

// Diligent mesh data (GPU buffers)
struct DiligentMeshData {
    Diligent::IBuffer* vertexBuffer = nullptr;
    Diligent::IBuffer* indexBuffer = nullptr;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};

// Light data for Diligent
struct DiligentLightData {
    glm::vec3 direction;   // For directional
    glm::vec3 position;    // For point/spot
    glm::vec3 color;
    float intensity;
    float range;           // For point
    float innerCone;       // For spot
    float outerCone;       // For spot
    int type;              // 0=directional, 1=point, 2=spot
    bool castShadows;
};

class DiligentPBR {
public:
    DiligentPBR();
    ~DiligentPBR();

    // Initialize with Diligent renderer
    bool init(DiligentRenderer& renderer);
    void shutdown();

    // Create mesh from Vivid vertex data
    DiligentMeshData createMesh(const std::vector<Vertex3D>& vertices,
                                 const std::vector<uint32_t>& indices);
    void destroyMesh(DiligentMeshData& mesh);

    // Render a mesh with PBR material
    void render(const DiligentMeshData& mesh,
                const Camera3D& camera,
                const glm::mat4& transform,
                const PBRMaterial& material,
                const std::vector<DiligentLightData>& lights,
                Diligent::ITextureView* renderTarget,
                Diligent::ITextureView* depthTarget,
                bool clearRT = true,
                const glm::vec4& clearColor = {0, 0, 0, 1});

    // Shadow mapping
    void beginShadowPass(const DiligentLightData& light,
                         const glm::vec3& sceneCenter,
                         float sceneRadius);
    void renderToShadowMap(const DiligentMeshData& mesh,
                           const glm::mat4& transform);
    void endShadowPass();

    // Get shadow map for sampling
    Diligent::ITextureView* getShadowMapSRV();
    glm::mat4 getLightViewProjection() const { return lightViewProj_; }

    bool isValid() const { return initialized_; }

private:
    bool createPipelines();
    bool createShadowMapResources(int resolution = 2048);
    void updateFrameConstants(const Camera3D& camera,
                              const std::vector<DiligentLightData>& lights);

    DiligentRenderer* renderer_ = nullptr;
    bool initialized_ = false;

    // Pipeline states
    Diligent::IPipelineState* pbrPipeline_ = nullptr;
    Diligent::IPipelineState* shadowPipeline_ = nullptr;
    Diligent::IShaderResourceBinding* pbrSRB_ = nullptr;
    Diligent::IShaderResourceBinding* shadowSRB_ = nullptr;

    // Constant buffers
    Diligent::IBuffer* modelConstantsCB_ = nullptr;
    Diligent::IBuffer* frameConstantsCB_ = nullptr;
    Diligent::IBuffer* materialCB_ = nullptr;
    Diligent::IBuffer* lightsCB_ = nullptr;
    Diligent::IBuffer* shadowConstantsCB_ = nullptr;

    // Shadow map resources
    Diligent::ITexture* shadowMapTexture_ = nullptr;
    Diligent::ITextureView* shadowMapDSV_ = nullptr;
    Diligent::ITextureView* shadowMapSRV_ = nullptr;
    int shadowMapResolution_ = 2048;
    glm::mat4 lightViewProj_{1.0f};
};

} // namespace vivid

#endif // VIVID_USE_DILIGENT
