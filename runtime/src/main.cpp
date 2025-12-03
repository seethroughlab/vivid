// Vivid PBR Demo using DiligentFX PBR_Renderer
// This demonstrates proper usage of DiligentFX for PBR rendering with IBL

#include "vivid/diligent_renderer.h"
#include "vivid/texture_utils.h"
#include "vivid/mesh.h"
#include "vivid/camera.h"

#include "Buffer.h"
#include "ShaderResourceBinding.h"
#include "MapHelper.hpp"
#include "TextureUtilities.h"
#include "BasicMath.hpp"

// DiligentFX PBR headers
#include "PBR/interface/PBR_Renderer.hpp"

#include <iostream>
#include <cmath>
#include <vector>
#include <string>

// Include shader structures inside the proper namespace
// This is required because .fxh files use HLSL types that map to Diligent types
namespace Diligent
{
namespace HLSL
{
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"
} // namespace HLSL
} // namespace Diligent

using namespace Diligent;

// PBR Material with textures
struct PBRMaterialData {
    std::string name;
    vivid::ManagedTexture albedoMap;
    vivid::ManagedTexture normalMap;
    vivid::ManagedTexture metallicMap;
    vivid::ManagedTexture roughnessMap;
    vivid::ManagedTexture aoMap;
};

// Load a PBR material from a directory
PBRMaterialData loadMaterial(vivid::TextureUtils& texUtils, const std::string& basePath, const std::string& name) {
    PBRMaterialData mat;
    mat.name = name;

    std::cout << "[Material] Loading: " << name << std::endl;

    // Try different naming conventions
    std::vector<std::string> albedoNames = {"_albedo.png", "_Base_Color.png"};
    std::vector<std::string> normalNames = {"_normal-ogl.png", "_Normal.png"};
    std::vector<std::string> metallicNames = {"_metallic.png", "_Metallic.png"};
    std::vector<std::string> roughnessNames = {"_roughness.png", "_Roughness.png"};
    std::vector<std::string> aoNames = {"_ao.png", "_Ambient_Occlusion.png"};

    // Load albedo (sRGB)
    for (const auto& suffix : albedoNames) {
        std::string path = basePath + "/" + name + suffix;
        mat.albedoMap = texUtils.loadFromFile(path, true, true);
        if (mat.albedoMap) {
            std::cout << "  Albedo: " << path << std::endl;
            break;
        }
    }

    // Load normal (linear)
    for (const auto& suffix : normalNames) {
        std::string path = basePath + "/" + name + suffix;
        mat.normalMap = texUtils.loadFromFile(path, true, false);
        if (mat.normalMap) {
            std::cout << "  Normal: " << path << std::endl;
            break;
        }
    }

    // Load metallic (linear)
    for (const auto& suffix : metallicNames) {
        std::string path = basePath + "/" + name + suffix;
        mat.metallicMap = texUtils.loadFromFile(path, true, false);
        if (mat.metallicMap) {
            std::cout << "  Metallic: " << path << std::endl;
            break;
        }
    }

    // Load roughness (linear)
    for (const auto& suffix : roughnessNames) {
        std::string path = basePath + "/" + name + suffix;
        mat.roughnessMap = texUtils.loadFromFile(path, true, false);
        if (mat.roughnessMap) {
            std::cout << "  Roughness: " << path << std::endl;
            break;
        }
    }

    // Load AO (linear)
    for (const auto& suffix : aoNames) {
        std::string path = basePath + "/" + name + suffix;
        mat.aoMap = texUtils.loadFromFile(path, true, false);
        if (mat.aoMap) {
            std::cout << "  AO: " << path << std::endl;
            break;
        }
    }

    return mat;
}

int main(int argc, char* argv[]) {
    std::cout << "Vivid - Creative Coding Framework" << std::endl;
    std::cout << "===================================" << std::endl;
    std::cout << "DiligentFX PBR Renderer Demo" << std::endl;

    // Create renderer
    vivid::DiligentRenderer renderer;

    // Configure window
    vivid::RendererConfig config;
    config.windowTitle = "Vivid - DiligentFX PBR";
    config.windowWidth = 1280;
    config.windowHeight = 720;
    config.vsync = true;

    // Initialize
    if (!renderer.initialize(config)) {
        std::cerr << "Failed to initialize renderer" << std::endl;
        return 1;
    }

    // Get Diligent device and context
    auto* device = renderer.getDevice();
    auto* context = renderer.getContext();
    auto* swapChain = renderer.getSwapChain();

    // Create texture utilities
    vivid::MeshUtils meshUtils(device);
    vivid::TextureUtils texUtils(device);

    // Create a sphere mesh
    auto sphere = meshUtils.createSphere(0.8f, 64, 32);
    if (!sphere.vertexBuffer) {
        std::cerr << "Failed to create sphere mesh" << std::endl;
        return 1;
    }

    // Load PBR materials
    std::string assetsPath = "assets/materials";
    std::vector<PBRMaterialData> materials;

    std::vector<std::pair<std::string, std::string>> materialDirs = {
        {"bronze-bl", "bronze"},
        {"hexagon-pavers1-bl", "hexagon-pavers1"},
        {"roughrockface2-bl", "roughrockface2"},
        {"speckled-granite-tiles-bl", "speckled-granite-tiles"},
        {"square-damp-blocks-bl", "square-damp-blocks"},
        {"whispy-grass-meadow-bl", "wispy-grass-meadow"}
    };

    for (const auto& [dir, prefix] : materialDirs) {
        auto mat = loadMaterial(texUtils, assetsPath + "/" + dir, prefix);
        if (mat.albedoMap) {
            materials.push_back(std::move(mat));
        }
    }

    std::cout << "\nLoaded " << materials.size() << " materials" << std::endl;

    // ========================================
    // Initialize DiligentFX PBR_Renderer
    // ========================================
    std::cout << "\n[PBR] Initializing DiligentFX PBR_Renderer..." << std::endl;

    PBR_Renderer::CreateInfo pbrCI;
    pbrCI.EnableIBL = true;
    pbrCI.EnableAO = true;
    pbrCI.EnableEmissive = true;
    pbrCI.UseSeparateMetallicRoughnessTextures = true;  // We have separate metallic/roughness maps
    pbrCI.CreateDefaultTextures = true;
    pbrCI.MaxLightCount = 4;

    // Define input layout matching our Vertex3D structure:
    // struct Vertex3D { vec3 position; vec3 normal; vec2 uv; vec4 tangent; }
    // DiligentFX expects: Pos=ATTRIB0, Normal=ATTRIB1, UV0=ATTRIB2, Tangent=ATTRIB7
    // We use AUTO_OFFSET and AUTO_STRIDE to handle contiguous vertex data
    std::vector<LayoutElement> inputLayout = {
        // InputIndex, BufferSlot, NumComponents, ValueType, IsNormalized, RelativeOffset, Stride, Frequency
        {0, 0, 3, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE, INPUT_ELEMENT_FREQUENCY_PER_VERTEX},  // Position
        {1, 0, 3, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE, INPUT_ELEMENT_FREQUENCY_PER_VERTEX},  // Normal
        {2, 0, 2, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE, INPUT_ELEMENT_FREQUENCY_PER_VERTEX},  // UV
        {7, 0, 4, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE, INPUT_ELEMENT_FREQUENCY_PER_VERTEX},  // Tangent at ATTRIB7
    };
    pbrCI.InputLayout = InputLayoutDesc{inputLayout.data(), static_cast<Uint32>(inputLayout.size())};

    std::unique_ptr<PBR_Renderer> pbrRenderer;
    try {
        pbrRenderer = std::make_unique<PBR_Renderer>(device, nullptr, context, pbrCI);
        std::cout << "[PBR] PBR_Renderer created successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[PBR] Failed to create PBR_Renderer: " << e.what() << std::endl;
        return 1;
    }

    // Load HDR environment map and precompute IBL cubemaps
    bool useIBL = false;
    {
        std::string hdrPath = "assets/hdris/783-hdri-skies-com.hdr";
        std::cout << "[IBL] Loading HDR environment: " << hdrPath << std::endl;

        RefCntAutoPtr<ITexture> envMapTexture;
        TextureLoadInfo loadInfo;
        loadInfo.Name = "Environment Map";
        loadInfo.IsSRGB = false;
        loadInfo.GenerateMips = false;

        CreateTextureFromFile(hdrPath.c_str(), loadInfo, device, &envMapTexture);

        if (envMapTexture) {
            std::cout << "[IBL] HDR loaded, precomputing IBL cubemaps..." << std::endl;
            auto* envMapSRV = envMapTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            pbrRenderer->PrecomputeCubemaps(context, envMapSRV);
            useIBL = true;
            std::cout << "[IBL] IBL cubemaps generated successfully!" << std::endl;
        } else {
            std::cerr << "[IBL] Failed to load HDR environment map" << std::endl;
        }
    }

    // Create frame attributes buffer
    Uint32 frameAttribsSize = pbrRenderer->GetPRBFrameAttribsSize(pbrCI.MaxLightCount, 0);
    RefCntAutoPtr<IBuffer> frameAttribsBuffer;
    {
        BufferDesc desc;
        desc.Name = "PBR Frame Attribs";
        desc.Size = frameAttribsSize;
        desc.Usage = USAGE_DYNAMIC;
        desc.BindFlags = BIND_UNIFORM_BUFFER;
        desc.CPUAccessFlags = CPU_ACCESS_WRITE;
        device->CreateBuffer(desc, nullptr, &frameAttribsBuffer);
    }

    // Set up graphics pipeline description for PSO cache
    GraphicsPipelineDesc graphicsDesc;
    graphicsDesc.NumRenderTargets = 1;
    graphicsDesc.RTVFormats[0] = swapChain->GetDesc().ColorBufferFormat;
    graphicsDesc.DSVFormat = swapChain->GetDesc().DepthBufferFormat;
    graphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphicsDesc.RasterizerDesc.CullMode = CULL_MODE_BACK;
    graphicsDesc.RasterizerDesc.FrontCounterClockwise = False;
    graphicsDesc.DepthStencilDesc.DepthEnable = True;
    graphicsDesc.DepthStencilDesc.DepthWriteEnable = True;
    graphicsDesc.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS;

    auto psoCache = pbrRenderer->GetPsoCacheAccessor(graphicsDesc);

    // Create SRBs for each material
    std::vector<RefCntAutoPtr<IShaderResourceBinding>> materialSrbs;
    for (size_t i = 0; i < materials.size(); ++i) {
        RefCntAutoPtr<IShaderResourceBinding> srb;
        pbrRenderer->CreateResourceBinding(&srb);

        if (srb) {
            // Initialize common SRB variables (IBL textures, frame attribs, etc.)
            pbrRenderer->InitCommonSRBVars(srb, frameAttribsBuffer, true, nullptr);

            // Set material textures
            auto& mat = materials[i];

            if (mat.albedoMap) {
                pbrRenderer->SetMaterialTexture(srb, mat.albedoMap.srv,
                    PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR);
            }
            if (mat.normalMap) {
                pbrRenderer->SetMaterialTexture(srb, mat.normalMap.srv,
                    PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL);
            }
            if (mat.metallicMap) {
                pbrRenderer->SetMaterialTexture(srb, mat.metallicMap.srv,
                    PBR_Renderer::TEXTURE_ATTRIB_ID_METALLIC);
            }
            if (mat.roughnessMap) {
                pbrRenderer->SetMaterialTexture(srb, mat.roughnessMap.srv,
                    PBR_Renderer::TEXTURE_ATTRIB_ID_ROUGHNESS);
            }
            if (mat.aoMap) {
                pbrRenderer->SetMaterialTexture(srb, mat.aoMap.srv,
                    PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION);
            }

            materialSrbs.push_back(srb);
        }
    }

    std::cout << "[PBR] Created " << materialSrbs.size() << " material bindings" << std::endl;
    std::cout << "[PBR] IBL enabled: " << (useIBL ? "YES" : "NO") << std::endl;

    // Setup camera
    vivid::Camera3D camera;
    camera.setPosition(glm::vec3(0.0f, 2.0f, 6.0f));
    camera.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.setFOV(60.0f);
    camera.setAspectRatio(1280.0f / 720.0f);
    camera.setNearPlane(0.1f);
    camera.setFarPlane(100.0f);

    // Grid layout
    const int gridCols = 3;
    const float spacing = 2.0f;

    std::cout << "\nStarting render loop..." << std::endl;
    std::cout << "Displaying " << materials.size() << " PBR materials with DiligentFX" << std::endl;

    // Build PSO flags for our materials
    PBR_Renderer::PSO_FLAGS psoFlags =
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
        PBR_Renderer::PSO_FLAG_USE_METALLIC_MAP |
        PBR_Renderer::PSO_FLAG_USE_ROUGHNESS_MAP |
        PBR_Renderer::PSO_FLAG_USE_AO_MAP |
        PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS |
        PBR_Renderer::PSO_FLAG_USE_VERTEX_TANGENTS |
        PBR_Renderer::PSO_FLAG_USE_TEXCOORD0 |
        PBR_Renderer::PSO_FLAG_USE_LIGHTS |
        PBR_Renderer::PSO_FLAG_ENABLE_TONE_MAPPING;

    if (useIBL) {
        psoFlags |= PBR_Renderer::PSO_FLAG_USE_IBL;
    }

    // Main loop
    while (!renderer.shouldClose()) {
        renderer.pollEvents();
        renderer.beginFrame();

        double time = renderer.getTime();

        // Clear with dark background
        renderer.clear(0.02f, 0.02f, 0.05f, 1.0f);

        // Get PSO for our material configuration
        PBR_Renderer::PSOKey psoKey(psoFlags, PBR_Renderer::ALPHA_MODE_OPAQUE, CULL_MODE_BACK);
        IPipelineState* pso = psoCache.Get(psoKey, PBR_Renderer::PsoCacheAccessor::GET_FLAG_CREATE_IF_NULL);

        if (pso && !materialSrbs.empty()) {
            // Update frame attributes buffer
            {
                MapHelper<Uint8> frameData(context, frameAttribsBuffer, MAP_WRITE, MAP_FLAG_DISCARD);

                // Cast to structured data - layout matches RenderPBR_Structures.fxh
                Uint8* frameAttribs = frameData;

                // Fill CameraAttribs (at offset 0)
                HLSL::CameraAttribs* camAttribs = reinterpret_cast<HLSL::CameraAttribs*>(frameAttribs);
                glm::vec3 camPos = camera.getPosition();
                camAttribs->f4Position = float4(camPos.x, camPos.y, camPos.z, 1.0f);
                camAttribs->f4ViewportSize = float4(1280.0f, 720.0f, 1.0f/1280.0f, 1.0f/720.0f);
                camAttribs->fNearPlaneZ = camera.getNearPlane();
                camAttribs->fFarPlaneZ = camera.getFarPlane();
                camAttribs->fHandness = 1.0f;  // Right-handed

                // Fill matrices
                glm::mat4 view = camera.getViewMatrix();
                glm::mat4 proj = camera.getProjectionMatrix();
                glm::mat4 viewProj = camera.getViewProjectionMatrix();

                std::memcpy(&camAttribs->mView, &view, sizeof(float4x4));
                std::memcpy(&camAttribs->mProj, &proj, sizeof(float4x4));
                std::memcpy(&camAttribs->mViewProj, &viewProj, sizeof(float4x4));

                glm::mat4 viewInv = glm::inverse(view);
                glm::mat4 projInv = glm::inverse(proj);
                glm::mat4 viewProjInv = glm::inverse(viewProj);
                std::memcpy(&camAttribs->mViewInv, &viewInv, sizeof(float4x4));
                std::memcpy(&camAttribs->mProjInv, &projInv, sizeof(float4x4));
                std::memcpy(&camAttribs->mViewProjInv, &viewProjInv, sizeof(float4x4));

                // PrevCamera follows Camera (for motion vectors) - use same values
                HLSL::CameraAttribs* prevCamAttribs = camAttribs + 1;
                std::memcpy(prevCamAttribs, camAttribs, sizeof(HLSL::CameraAttribs));

                // PBRRendererShaderParameters follows PrevCamera
                HLSL::PBRRendererShaderParameters* rendererParams = reinterpret_cast<HLSL::PBRRendererShaderParameters*>(prevCamAttribs + 1);
                rendererParams->AverageLogLum = 0.3f;
                rendererParams->MiddleGray = 0.18f;
                rendererParams->WhitePoint = 3.0f;
                rendererParams->PrefilteredCubeLastMip = 4.0f;
                rendererParams->IBLScale = float4(1.0f, 1.0f, 1.0f, 1.0f);
                rendererParams->OcclusionStrength = 1.0f;
                rendererParams->EmissionScale = 1.0f;
                rendererParams->LightCount = 1;

                // PBRLightAttribs array follows renderer params
                HLSL::PBRLightAttribs* lights = reinterpret_cast<HLSL::PBRLightAttribs*>(rendererParams + 1);

                // Rotating directional light
                float lightAngle = static_cast<float>(time) * 0.3f;
                lights[0].Type = 1;  // Directional
                lights[0].DirectionX = std::sin(lightAngle);
                lights[0].DirectionY = -0.6f;
                lights[0].DirectionZ = std::cos(lightAngle);
                lights[0].IntensityR = 3.0f;
                lights[0].IntensityG = 2.9f;
                lights[0].IntensityB = 2.8f;
                lights[0].ShadowMapIndex = -1;
            }

            // Set pipeline
            context->SetPipelineState(pso);

            // Bind mesh
            IBuffer* vertexBuffers[] = {sphere.vertexBuffer};
            Uint64 offsets[] = {0};
            context->SetVertexBuffers(0, 1, vertexBuffers, offsets,
                RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
            context->SetIndexBuffer(sphere.indexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            // Get primitive attribs buffer
            IBuffer* primAttribsCB = pbrRenderer->GetPBRPrimitiveAttribsCB();

            // Draw each material
            for (size_t i = 0; i < materials.size() && i < materialSrbs.size(); ++i) {
                // Calculate position in grid
                int col = i % gridCols;
                int row = i / gridCols;
                float posX = (col - (gridCols - 1) * 0.5f) * spacing;
                float posY = (1 - row) * spacing * 0.8f;

                // Slow rotation
                float rotAngle = static_cast<float>(time) * 0.2f + i * 0.5f;

                // Update primitive attributes
                {
                    MapHelper<Uint8> primData(context, primAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD);

                    // GLTFNodeShaderTransforms
                    Uint8* primAttribs = primData;
                    HLSL::GLTFNodeShaderTransforms* transforms = reinterpret_cast<HLSL::GLTFNodeShaderTransforms*>(primAttribs);

                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(posX, posY, 0.0f));
                    model = glm::rotate(model, rotAngle, glm::vec3(0.0f, 1.0f, 0.0f));
                    std::memcpy(&transforms->NodeMatrix, &model, sizeof(float4x4));
                    transforms->JointCount = 0;

                    // PBRMaterialShaderInfo follows transforms
                    HLSL::PBRMaterialShaderInfo* matInfo = reinterpret_cast<HLSL::PBRMaterialShaderInfo*>(transforms + 1);

                    // Basic material attributes
                    matInfo->Basic.BaseColorFactor = float4(1.0f, 1.0f, 1.0f, 1.0f);
                    matInfo->Basic.MetallicFactor = 1.0f;
                    matInfo->Basic.RoughnessFactor = 1.0f;
                    matInfo->Basic.OcclusionFactor = 1.0f;
                    matInfo->Basic.NormalScale = 1.0f;
                    matInfo->Basic.Workflow = PBR_WORKFLOW_METALLIC_ROUGHNESS;
                    matInfo->Basic.AlphaMode = PBR_ALPHA_MODE_OPAQUE;
                    matInfo->Basic.AlphaMaskCutoff = 0.5f;
                }

                // Commit shader resources and draw
                context->CommitShaderResources(materialSrbs[i], RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                DrawIndexedAttribs drawAttribs;
                drawAttribs.IndexType = VT_UINT32;
                drawAttribs.NumIndices = sphere.indexCount;
                drawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;
                context->DrawIndexed(drawAttribs);
            }
        }

        renderer.endFrame();
        renderer.present();
    }

    std::cout << "Shutting down..." << std::endl;

    pbrRenderer.reset();
    renderer.shutdown();

    return 0;
}
