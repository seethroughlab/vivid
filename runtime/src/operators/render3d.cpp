// Render3D Operator Implementation
// Uses DiligentFX PBR_Renderer for physically-based rendering

#include "vivid/operators/render3d.h"
#include "vivid/context.h"
#include "vivid/pbr_material.h"
#include "vivid/ibl.h"

#include <iostream>
#include <cstring>
#include <cmath>

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"
#include "GraphicsTypesX.hpp"
#include "PBR_Renderer.hpp"
#include "BasicMath.hpp"

// Include HLSL structures from DiligentFX
namespace Diligent {
namespace HLSL {
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
} // namespace HLSL
} // namespace Diligent

namespace vivid {

using namespace Diligent;

// Convert glm::mat4 to Diligent float4x4
// GLM uses column-major (mat[col][row]), Diligent uses row-major (mat[row][col])
// So we need to transpose during conversion
static float4x4 ToFloat4x4(const glm::mat4& m) {
    float4x4 result;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            result[row][col] = m[col][row];  // Transpose: GLM col/row -> Diligent row/col
        }
    }
    return result;
}

// Convert glm::vec3 to Diligent float3
static float3 ToFloat3(const glm::vec3& v) {
    return float3(v.x, v.y, v.z);
}

// Convert glm::vec4 to Diligent float4
static float4 ToFloat4(const glm::vec4& v) {
    return float4(v.x, v.y, v.z, v.w);
}

Render3D::Render3D() {
    // Add a default directional light
    Light3D defaultLight;
    defaultLight.type = Light3D::Type::Directional;
    defaultLight.direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));
    defaultLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
    defaultLight.intensity = 1.0f;
    lights_.push_back(defaultLight);
}

Render3D::~Render3D() {
    cleanup();
}

void Render3D::init(Context& ctx) {
    createRenderTargets(ctx);
    createPipeline(ctx);
}

void Render3D::createRenderTargets(Context& ctx) {
    outputWidth_ = ctx.width();
    outputHeight_ = ctx.height();

    auto* device = ctx.device();

    // Create color render target
    TextureDesc colorDesc;
    colorDesc.Name = "Render3D Color";
    colorDesc.Type = RESOURCE_DIM_TEX_2D;
    colorDesc.Width = outputWidth_;
    colorDesc.Height = outputHeight_;
    colorDesc.Format = TEX_FORMAT_RGBA8_UNORM;
    colorDesc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

    device->CreateTexture(colorDesc, nullptr, &colorTexture_);
    colorRTV_ = colorTexture_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    colorSRV_ = colorTexture_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    // Create depth buffer
    TextureDesc depthDesc;
    depthDesc.Name = "Render3D Depth";
    depthDesc.Type = RESOURCE_DIM_TEX_2D;
    depthDesc.Width = outputWidth_;
    depthDesc.Height = outputHeight_;
    depthDesc.Format = TEX_FORMAT_D32_FLOAT;
    depthDesc.BindFlags = BIND_DEPTH_STENCIL;

    device->CreateTexture(depthDesc, nullptr, &depthTexture_);
    depthDSV_ = depthTexture_->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
}

void Render3D::createPipeline(Context& ctx) {
    auto* device = ctx.device();
    auto* context = ctx.immediateContext();

    // Configure PBR_Renderer
    PBR_Renderer::CreateInfo pbrCI;
    pbrCI.EnableIBL = true;  // Enable IBL for environment reflections
    pbrCI.EnableAO = true;    // Enable AO for textured materials
    pbrCI.EnableEmissive = false;
    pbrCI.EnableClearCoat = false;
    pbrCI.EnableSheen = false;
    pbrCI.EnableAnisotropy = false;
    pbrCI.EnableIridescence = false;
    pbrCI.EnableTransmission = false;
    pbrCI.EnableVolume = false;
    pbrCI.CreateDefaultTextures = true;
    pbrCI.EnableShadows = false;
    pbrCI.UseSeparateMetallicRoughnessTextures = true;  // Use separate metallic/roughness maps

    // Configure texture attribute indices - these map TEXTURE_ATTRIB_ID to material texture slots
    // Required for shader macros like MetallicTextureAttribId to be defined
    pbrCI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = 0;
    pbrCI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL] = 1;
    pbrCI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_METALLIC] = 2;
    pbrCI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_ROUGHNESS] = 3;
    pbrCI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION] = 4;

    // Configure max lights
    pbrCI.MaxLightCount = 4;
    pbrCI.MaxShadowCastingLightCount = 0;

    // Input layout for our Vertex3D structure (44 bytes total)
    // Layout: position(vec3, 12), normal(vec3, 12), uv(vec2, 8), tangent(vec3, 12)
    // DiligentFX expects: ATTRIB0=Pos, ATTRIB1=Normal, ATTRIB2=UV0, ATTRIB7=Tangent
    // Must explicitly set stride for ALL elements since we have non-contiguous indices
    constexpr Uint32 stride = sizeof(Vertex3D);  // 44 bytes
    LayoutElement layoutElements[] = {
        {0, 0, 3, VT_FLOAT32, False, 0, stride},   // Position at offset 0
        {1, 0, 3, VT_FLOAT32, False, 12, stride},  // Normal at offset 12
        {2, 0, 2, VT_FLOAT32, False, 24, stride},  // UV at offset 24
        {7, 0, 3, VT_FLOAT32, False, 32, stride},  // Tangent at offset 32
    };
    pbrCI.InputLayout.LayoutElements = layoutElements;
    pbrCI.InputLayout.NumElements = _countof(layoutElements);

    // Create PBR_Renderer
    pbrRenderer_ = std::make_unique<PBR_Renderer>(device, nullptr, context, pbrCI);

    if (!pbrRenderer_) {
        std::cerr << "Failed to create PBR_Renderer" << std::endl;
        return;
    }
    std::cout << "Created DiligentFX PBR_Renderer" << std::endl;

    // Create frame attribs buffer
    // Size depends on max lights
    Uint32 frameAttribsSize = PBR_Renderer::GetPRBFrameAttribsSize(pbrCI.MaxLightCount, 0);

    BufferDesc bufDesc;
    bufDesc.Name = "PBR Frame Attribs";
    bufDesc.Usage = USAGE_DYNAMIC;
    bufDesc.BindFlags = BIND_UNIFORM_BUFFER;
    bufDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
    bufDesc.Size = frameAttribsSize;
    device->CreateBuffer(bufDesc, nullptr, &frameAttribsBuffer_);

    // Create primitive attribs buffer (per-object transforms)
    PBR_Renderer::PSO_FLAGS psoFlags = PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS |
                                        PBR_Renderer::PSO_FLAG_USE_TEXCOORD0 |
                                        PBR_Renderer::PSO_FLAG_USE_LIGHTS;
    Uint32 primAttribsSize = pbrRenderer_->GetPBRPrimitiveAttribsSize(psoFlags);

    bufDesc.Name = "PBR Primitive Attribs";
    bufDesc.Size = primAttribsSize;
    device->CreateBuffer(bufDesc, nullptr, &primitiveAttribsBuffer_);

    // Create material attribs buffer
    Uint32 matAttribsSize = pbrRenderer_->GetPBRMaterialAttribsSize(psoFlags);

    bufDesc.Name = "PBR Material Attribs";
    bufDesc.Size = matAttribsSize;
    device->CreateBuffer(bufDesc, nullptr, &materialAttribsBuffer_);

    // Calculate size for textured PSO (to verify buffer size)
    PBR_Renderer::PSO_FLAGS texturedFlags = psoFlags |
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
        PBR_Renderer::PSO_FLAG_USE_METALLIC_MAP |
        PBR_Renderer::PSO_FLAG_USE_ROUGHNESS_MAP |
        PBR_Renderer::PSO_FLAG_USE_AO_MAP;
    Uint32 texturedMatSize = pbrRenderer_->GetPBRMaterialAttribsSize(texturedFlags);

    std::cout << "Created PBR constant buffers (frame=" << frameAttribsSize
              << ", prim=" << primAttribsSize << ", mat(base)=" << matAttribsSize
              << ", mat(textured)=" << texturedMatSize << ")" << std::endl;

    // Check internal buffer size
    auto* internalMatCB = pbrRenderer_->GetPBRMaterialAttribsCB();
    if (internalMatCB) {
        std::cout << "Internal material CB size: " << internalMatCB->GetDesc().Size << std::endl;
    }
}

void Render3D::process(Context& ctx) {
    // Update camera aspect ratio
    camera_.setAspectRatio(static_cast<float>(ctx.width()) / static_cast<float>(ctx.height()));

    renderScene(ctx);
}

void Render3D::renderScene(Context& ctx) {
    auto* context = ctx.immediateContext();

    // Set render target
    ITextureView* rtv = colorRTV_;
    context->SetRenderTargets(1, &rtv, depthDSV_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Clear
    float clearColor[] = {backgroundColor_.r, backgroundColor_.g, backgroundColor_.b, backgroundColor_.a};
    context->ClearRenderTarget(rtv, clearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearDepthStencil(depthDSV_, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set viewport
    Viewport vp;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = static_cast<float>(outputWidth_);
    vp.Height = static_cast<float>(outputHeight_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->SetViewports(1, &vp, outputWidth_, outputHeight_);

    if (!pbrRenderer_ || objects_.empty()) return;

    // Configure graphics pipeline description for PSO lookup
    GraphicsPipelineDesc graphicsDesc;
    graphicsDesc.NumRenderTargets = 1;
    graphicsDesc.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    graphicsDesc.DSVFormat = TEX_FORMAT_D32_FLOAT;
    graphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphicsDesc.RasterizerDesc.CullMode = CULL_MODE_BACK;
    graphicsDesc.RasterizerDesc.FrontCounterClockwise = True;
    graphicsDesc.DepthStencilDesc.DepthEnable = True;
    graphicsDesc.DepthStencilDesc.DepthWriteEnable = True;
    graphicsDesc.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS;

    // Get PSO cache accessor
    auto psoCache = pbrRenderer_->GetPsoCacheAccessor(graphicsDesc);
    if (!psoCache) {
        std::cerr << "Failed to get PSO cache accessor" << std::endl;
        return;
    }

    // Base PSO flags for our rendering
    PBR_Renderer::PSO_FLAGS basePsoFlags =
        PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS |
        PBR_Renderer::PSO_FLAG_USE_TEXCOORD0 |
        PBR_Renderer::PSO_FLAG_USE_LIGHTS;

    // Add IBL flag if environment is set
    if (environment_ && environment_->isLoaded()) {
        basePsoFlags |= PBR_Renderer::PSO_FLAG_USE_IBL;
    }

    // Textured PSO flags (includes all texture maps)
    PBR_Renderer::PSO_FLAGS texturedPsoFlags = basePsoFlags |
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
        PBR_Renderer::PSO_FLAG_USE_METALLIC_MAP |
        PBR_Renderer::PSO_FLAG_USE_ROUGHNESS_MAP |
        PBR_Renderer::PSO_FLAG_USE_AO_MAP |
        PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM;

    // Update frame attribs buffer
    {
        MapHelper<Uint8> frameData(context, frameAttribsBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
        if (!frameData) {
            std::cerr << "Failed to map frame attribs buffer" << std::endl;
            return;
        }

        // The buffer layout depends on PBR_MAX_LIGHTS define
        // Structure: CameraAttribs + CameraAttribs(prev) + PBRRendererShaderParameters + PBRLightAttribs[N]
        Uint8* ptr = frameData;

        // Camera attribs
        HLSL::CameraAttribs* camera = reinterpret_cast<HLSL::CameraAttribs*>(ptr);
        memset(camera, 0, sizeof(HLSL::CameraAttribs));

        camera->f4Position = ToFloat4(glm::vec4(camera_.position(), 1.0f));
        camera->f4ViewportSize = float4(
            static_cast<float>(outputWidth_),
            static_cast<float>(outputHeight_),
            1.0f / outputWidth_,
            1.0f / outputHeight_
        );
        camera->fNearPlaneZ = camera_.nearPlane();
        camera->fFarPlaneZ = camera_.farPlane();
        camera->fHandness = 1.0f;  // Right-handed

        // View and projection matrices
        glm::mat4 view = camera_.viewMatrix();
        glm::mat4 proj = camera_.projectionMatrix();
        glm::mat4 viewProj = proj * view;

        camera->mView = ToFloat4x4(view);
        camera->mProj = ToFloat4x4(proj);
        camera->mViewProj = ToFloat4x4(viewProj);
        camera->mViewInv = ToFloat4x4(glm::inverse(view));
        camera->mProjInv = ToFloat4x4(glm::inverse(proj));
        camera->mViewProjInv = ToFloat4x4(glm::inverse(viewProj));

        ptr += sizeof(HLSL::CameraAttribs);

        // Previous camera (same as current for now)
        HLSL::CameraAttribs* prevCamera = reinterpret_cast<HLSL::CameraAttribs*>(ptr);
        memcpy(prevCamera, camera, sizeof(HLSL::CameraAttribs));
        ptr += sizeof(HLSL::CameraAttribs);

        // Renderer shader parameters
        HLSL::PBRRendererShaderParameters* renderer = reinterpret_cast<HLSL::PBRRendererShaderParameters*>(ptr);
        memset(renderer, 0, sizeof(HLSL::PBRRendererShaderParameters));

        renderer->AverageLogLum = 0.3f;
        renderer->MiddleGray = 0.18f;
        renderer->WhitePoint = 3.0f;
        renderer->OcclusionStrength = 1.0f;
        renderer->EmissionScale = 1.0f;
        renderer->IBLScale = float4(1.0f, 1.0f, 1.0f, 1.0f);
        renderer->LightCount = static_cast<int>(std::min(lights_.size(), size_t(4)));

        ptr += sizeof(HLSL::PBRRendererShaderParameters);

        // Lights
        for (size_t i = 0; i < 4 && i < lights_.size(); ++i) {
            HLSL::PBRLightAttribs* light = reinterpret_cast<HLSL::PBRLightAttribs*>(ptr);
            memset(light, 0, sizeof(HLSL::PBRLightAttribs));

            const Light3D& src = lights_[i];
            switch (src.type) {
                case Light3D::Type::Directional:
                    light->Type = 1;
                    light->DirectionX = src.direction.x;
                    light->DirectionY = src.direction.y;
                    light->DirectionZ = src.direction.z;
                    break;

                case Light3D::Type::Point:
                    light->Type = 2;
                    light->PosX = src.position.x;
                    light->PosY = src.position.y;
                    light->PosZ = src.position.z;
                    if (src.range > 0) {
                        float r4 = src.range * src.range * src.range * src.range;
                        light->Range4 = r4;
                    }
                    break;

                case Light3D::Type::Spot:
                    light->Type = 3;
                    light->PosX = src.position.x;
                    light->PosY = src.position.y;
                    light->PosZ = src.position.z;
                    light->DirectionX = src.direction.x;
                    light->DirectionY = src.direction.y;
                    light->DirectionZ = src.direction.z;
                    if (src.range > 0) {
                        float r4 = src.range * src.range * src.range * src.range;
                        light->Range4 = r4;
                    }
                    // SpotAngleScale = 1.0 / (cos(inner) - cos(outer))
                    // SpotAngleOffset = -cos(outer) * SpotAngleScale
                    {
                        float cosInner = std::cos(src.innerConeAngle);
                        float cosOuter = std::cos(src.outerConeAngle);
                        float denom = cosInner - cosOuter;
                        if (std::abs(denom) > 1e-6f) {
                            light->SpotAngleScale = 1.0f / denom;
                            light->SpotAngleOffset = -cosOuter * light->SpotAngleScale;
                        }
                    }
                    break;
            }

            light->IntensityR = src.color.r * src.intensity;
            light->IntensityG = src.color.g * src.intensity;
            light->IntensityB = src.color.b * src.intensity;
            light->ShadowMapIndex = -1;

            ptr += sizeof(HLSL::PBRLightAttribs);
        }
    }

    // Draw each object
    IPipelineState* currentPso = nullptr;
    IShaderResourceBinding* currentSrb = nullptr;
    const PBRMaterial* currentMaterial = nullptr;

    for (const Object3D& obj : objects_) {
        if (!obj.mesh || !obj.mesh->vertexBuffer()) continue;

        // Determine PSO flags based on material
        bool hasTextures = obj.material != nullptr;
        PBR_Renderer::PSO_FLAGS psoFlags = hasTextures ? texturedPsoFlags : basePsoFlags;

        // Get PSO for this object
        PBR_Renderer::PSOKey psoKey(
            PBR_Renderer::RenderPassType::Main,
            psoFlags,
            PBR_Renderer::ALPHA_MODE_OPAQUE,
            CULL_MODE_BACK
        );

        IPipelineState* pso = psoCache.Get(psoKey, PBR_Renderer::PsoCacheAccessor::GET_FLAG_CREATE_IF_NULL);
        if (!pso) {
            std::cerr << "Failed to get PBR PSO" << std::endl;
            continue;
        }

        // Check if we need a new SRB (PSO changed or material changed)
        bool needNewSrb = (pso != currentPso) || (obj.material != currentMaterial);

        // Switch PSO if needed
        if (pso != currentPso) {
            currentPso = pso;
            context->SetPipelineState(pso);
        }

        // Create new SRB if PSO or material changed
        if (needNewSrb) {
            if (currentSrb) {
                currentSrb->Release();
            }
            pbrRenderer_->CreateResourceBinding(&currentSrb);
            if (!currentSrb) {
                std::cerr << "Failed to create SRB" << std::endl;
                continue;
            }

            // Bind frame attribs with optional IBL prefiltered env map
            ITextureView* prefilteredEnvMap = nullptr;
            if (environment_ && environment_->isLoaded()) {
                prefilteredEnvMap = environment_->prefilteredSRV();
            }
            pbrRenderer_->InitCommonSRBVars(currentSrb, frameAttribsBuffer_, true, true, prefilteredEnvMap);

            // Bind additional IBL textures if environment is set
            if (environment_ && environment_->isLoaded()) {
                // Bind irradiance map for diffuse IBL
                if (auto* var = currentSrb->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceMap")) {
                    var->Set(environment_->irradianceSRV());
                }
                // Bind BRDF LUT for specular IBL
                if (auto* var = currentSrb->GetVariableByName(SHADER_TYPE_PIXEL, "g_PreintegratedGGX")) {
                    var->Set(environment_->brdfLutSRV());
                }
            }

            // Bind textures if material is present
            if (hasTextures && obj.material) {
                static bool debugOnce = true;
                if (debugOnce) {
                    debugOnce = false;
                    std::cout << "Binding textures to SRB:" << std::endl;
                    std::cout << "  albedoSRV: " << (obj.material->albedoSRV() ? "valid" : "NULL") << std::endl;
                    std::cout << "  normalSRV: " << (obj.material->normalSRV() ? "valid" : "NULL") << std::endl;
                    std::cout << "  metallicSRV: " << (obj.material->metallicSRV() ? "valid" : "NULL") << std::endl;
                    std::cout << "  roughnessSRV: " << (obj.material->roughnessSRV() ? "valid" : "NULL") << std::endl;
                    std::cout << "  aoSRV: " << (obj.material->aoSRV() ? "valid" : "NULL") << std::endl;

                    // Check if shader variables exist
                    auto checkVar = [&](const char* name) {
                        if (auto* var = currentSrb->GetVariableByName(SHADER_TYPE_PIXEL, name)) {
                            std::cout << "  Shader var '" << name << "': found" << std::endl;
                        } else {
                            std::cout << "  Shader var '" << name << "': NOT FOUND" << std::endl;
                        }
                    };
                    checkVar("g_BaseColorMap");
                    checkVar("g_NormalMap");
                    checkVar("g_MetallicMap");
                    checkVar("g_RoughnessMap");
                    checkVar("g_OcclusionMap");
                }

                pbrRenderer_->SetMaterialTexture(currentSrb, obj.material->albedoSRV(),
                    PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR);
                pbrRenderer_->SetMaterialTexture(currentSrb, obj.material->normalSRV(),
                    PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL);
                pbrRenderer_->SetMaterialTexture(currentSrb, obj.material->metallicSRV(),
                    PBR_Renderer::TEXTURE_ATTRIB_ID_METALLIC);
                pbrRenderer_->SetMaterialTexture(currentSrb, obj.material->roughnessSRV(),
                    PBR_Renderer::TEXTURE_ATTRIB_ID_ROUGHNESS);
                pbrRenderer_->SetMaterialTexture(currentSrb, obj.material->aoSRV(),
                    PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION);
            }

            currentMaterial = obj.material;
        }

        // Update primitive attribs
        {
            MapHelper<Uint8> primData(context, pbrRenderer_->GetPBRPrimitiveAttribsCB(), MAP_WRITE, MAP_FLAG_DISCARD);
            if (primData) {
                Uint8* ptr = primData;

                // Node matrix (world transform)
                float4x4* nodeMatrix = reinterpret_cast<float4x4*>(ptr);
                *nodeMatrix = ToFloat4x4(obj.transform);
                ptr += sizeof(float4x4);

                // Joint count and first joint
                int* jointCount = reinterpret_cast<int*>(ptr);
                *jointCount = 0;
                ptr += sizeof(int);

                int* firstJoint = reinterpret_cast<int*>(ptr);
                *firstJoint = 0;
                ptr += sizeof(int);

                // Position bias and scale (identity)
                float* posBias = reinterpret_cast<float*>(ptr);
                posBias[0] = posBias[1] = posBias[2] = 0.0f;
                ptr += sizeof(float) * 3;

                float* posScale = reinterpret_cast<float*>(ptr);
                posScale[0] = posScale[1] = posScale[2] = 1.0f;
                ptr += sizeof(float) * 3;

                // Fallback color
                float4* fallbackColor = reinterpret_cast<float4*>(ptr);
                *fallbackColor = ToFloat4(obj.color);
                ptr += sizeof(float4);

                // Custom data
                float4* customData = reinterpret_cast<float4*>(ptr);
                *customData = float4(0, 0, 0, 0);
            }
        }

        // Update material attribs
        {
            MapHelper<Uint8> matData(context, pbrRenderer_->GetPBRMaterialAttribsCB(), MAP_WRITE, MAP_FLAG_DISCARD);
            if (matData) {
                Uint8* ptr = matData;

                // Write PBRMaterialBasicAttribs first
                HLSL::PBRMaterialBasicAttribs* basic = reinterpret_cast<HLSL::PBRMaterialBasicAttribs*>(ptr);
                memset(basic, 0, sizeof(HLSL::PBRMaterialBasicAttribs));

                // For textured materials, use white base color so texture shows properly
                // For untextured materials, use the object's color
                if (hasTextures) {
                    basic->BaseColorFactor = float4(1.0f, 1.0f, 1.0f, 1.0f);
                    basic->MetallicFactor = 1.0f;   // Use texture values
                    basic->RoughnessFactor = 1.0f;  // Use texture values
                } else {
                    basic->BaseColorFactor = ToFloat4(obj.color);
                    basic->MetallicFactor = obj.metallic;
                    basic->RoughnessFactor = obj.roughness;
                }
                basic->OcclusionFactor = 1.0f;
                basic->Workflow = 0;  // Metallic-roughness
                basic->AlphaMode = 0;  // Opaque
                basic->AlphaMaskCutoff = 0.5f;
                basic->NormalScale = 1.0f;

                ptr += sizeof(HLSL::PBRMaterialBasicAttribs);

                // Write PBRMaterialTextureAttribs for each texture (5 textures: base color, normal, metallic, roughness, AO)
                // These define UV transformation and which UV set to use
                if (hasTextures) {
                    static bool debugOnce2 = true;
                    if (debugOnce2) {
                        debugOnce2 = false;
                        std::cout << "Writing " << 5 << " texture attribs to material buffer" << std::endl;
                        std::cout << "  Basic attribs size: " << sizeof(HLSL::PBRMaterialBasicAttribs) << std::endl;
                        std::cout << "  Each texture attrib size: " << sizeof(HLSL::PBRMaterialTextureAttribs) << std::endl;
                        std::cout << "  Total write size: " << (sizeof(HLSL::PBRMaterialBasicAttribs) + 5 * sizeof(HLSL::PBRMaterialTextureAttribs)) << std::endl;
                    }

                    for (int i = 0; i < 5; ++i) {
                        HLSL::PBRMaterialTextureAttribs* texAttrib = reinterpret_cast<HLSL::PBRMaterialTextureAttribs*>(ptr);

                        // PackedProps: UV selector in bits [0-2]
                        // Value of 1 means UV selector = 0 (use UV0) because unpacking does: (PackedProps & 7) - 1
                        texAttrib->PackedProps = 1;  // Use UV0
                        texAttrib->TextureSlice = 0.0f;
                        texAttrib->UBias = 0.0f;
                        texAttrib->VBias = 0.0f;

                        // Identity UV transform
                        texAttrib->UVScaleAndRotation = float4(1.0f, 0.0f, 0.0f, 1.0f);

                        // Atlas scale and bias (no atlas, full texture)
                        texAttrib->AtlasUVScaleAndBias = float4(1.0f, 1.0f, 0.0f, 0.0f);

                        ptr += sizeof(HLSL::PBRMaterialTextureAttribs);
                    }
                }
            }
        }

        // Bind vertex and index buffers
        IBuffer* vertexBuffers[] = {obj.mesh->vertexBuffer()};
        Uint64 offsets[] = {0};
        context->SetVertexBuffers(0, 1, vertexBuffers, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
        context->SetIndexBuffer(obj.mesh->indexBuffer(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Commit resources
        context->CommitShaderResources(currentSrb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Draw
        DrawIndexedAttribs drawAttrs;
        drawAttrs.IndexType = VT_UINT32;
        drawAttrs.NumIndices = obj.mesh->indexCount();
        drawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
        context->DrawIndexed(drawAttrs);
    }

    // Clean up temporary SRB
    if (currentSrb) {
        currentSrb->Release();
    }
}

void Render3D::cleanup() {
    if (srb_) { srb_->Release(); srb_ = nullptr; }
    if (materialAttribsBuffer_) { materialAttribsBuffer_->Release(); materialAttribsBuffer_ = nullptr; }
    if (primitiveAttribsBuffer_) { primitiveAttribsBuffer_->Release(); primitiveAttribsBuffer_ = nullptr; }
    if (frameAttribsBuffer_) { frameAttribsBuffer_->Release(); frameAttribsBuffer_ = nullptr; }
    pbrRenderer_.reset();
    if (depthDSV_) { depthDSV_ = nullptr; }
    if (depthTexture_) { depthTexture_->Release(); depthTexture_ = nullptr; }
    if (colorSRV_) { colorSRV_ = nullptr; }
    if (colorRTV_) { colorRTV_ = nullptr; }
    if (colorTexture_) { colorTexture_->Release(); colorTexture_ = nullptr; }
}

Diligent::ITextureView* Render3D::getOutputSRV() {
    return colorSRV_;
}

Diligent::ITextureView* Render3D::getOutputRTV() {
    return colorRTV_;
}

int Render3D::addObject(Mesh* mesh, const glm::mat4& transform) {
    Object3D obj;
    obj.mesh = mesh;
    obj.transform = transform;
    objects_.push_back(obj);
    return static_cast<int>(objects_.size() - 1);
}

Object3D* Render3D::getObject(int index) {
    if (index >= 0 && index < static_cast<int>(objects_.size())) {
        return &objects_[index];
    }
    return nullptr;
}

void Render3D::clearObjects() {
    objects_.clear();
}

int Render3D::addLight(const Light3D& light) {
    lights_.push_back(light);
    return static_cast<int>(lights_.size() - 1);
}

Light3D* Render3D::getLight(int index) {
    if (index >= 0 && index < static_cast<int>(lights_.size())) {
        return &lights_[index];
    }
    return nullptr;
}

void Render3D::setLight(int index, const Light3D& light) {
    if (index >= 0 && index < static_cast<int>(lights_.size())) {
        lights_[index] = light;
    }
}

void Render3D::clearLights() {
    lights_.clear();
}

Render3D& Render3D::backgroundColor(float r, float g, float b, float a) {
    backgroundColor_ = glm::vec4(r, g, b, a);
    return *this;
}

Render3D& Render3D::backgroundColor(const glm::vec4& color) {
    backgroundColor_ = color;
    return *this;
}

Render3D& Render3D::ambientColor(float r, float g, float b) {
    ambientColor_ = glm::vec3(r, g, b);
    return *this;
}

Render3D& Render3D::ambientColor(const glm::vec3& color) {
    ambientColor_ = color;
    return *this;
}

Render3D& Render3D::setEnvironment(IBLEnvironment* env) {
    environment_ = env;
    return *this;
}

} // namespace vivid
