#include "diligent_pbr.h"

#ifdef VIVID_USE_DILIGENT

#include "diligent_renderer.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "MapHelper.hpp"
#include "GraphicsUtilities.h"
#include "BasicMath.hpp"
#include <iostream>
#include <cstring>

namespace vivid {

// HLSL PBR Shader - DEBUG: Test with hardcoded triangle (bypass vertex buffer)
static const char* PBR_VS = R"(
struct VSInput {
    float3 Pos     : ATTRIB0;
    float3 Normal  : ATTRIB1;
    float2 UV      : ATTRIB2;
    uint VertexID  : SV_VertexID;
};

struct VSOutput {
    float4 Pos   : SV_POSITION;
    float3 Color : TEXCOORD0;
};

void main(in VSInput In, out VSOutput Out) {
    // DEBUG TEST: Hardcoded triangle - ignores vertex buffer data entirely
    // This tests if the pipeline itself works
    float2 positions[3] = {float2(-0.5, -0.5), float2(0.5, -0.5), float2(0.0, 0.5)};
    float3 colors[3] = {float3(1,0,0), float3(0,1,0), float3(0,0,1)};
    uint idx = In.VertexID % 3;
    Out.Pos = float4(positions[idx], 0.5, 1.0);
    Out.Color = colors[idx];

    // Uncomment below to test actual vertex data reading:
    // Out.Pos = float4(In.Pos.x * 0.3, In.Pos.y * 0.3, In.Pos.z * 0.3 + 0.5, 1.0);
    // Out.Color = In.Pos * 0.5 + 0.5;
}
)";

static const char* PBR_PS = R"(
struct VSOutput {
    float4 Pos   : SV_POSITION;
    float3 Color : TEXCOORD0;
};

float4 main(VSOutput In) : SV_Target {
    // DEBUG: Just output the color (vertex position mapped to color)
    return float4(In.Color, 1.0);
}
)";

// Shadow pass shader
static const char* Shadow_VS = R"(
cbuffer ShadowConstants : register(b0) {
    float4x4 LightViewProj;
    float4x4 Model;
};

struct VSInput {
    float3 Pos : ATTRIB0;
};

struct VSOutput {
    float4 Pos : SV_POSITION;
};

void main(in VSInput In, out VSOutput Out) {
    // DEBUG: Just pass through position scaled
    Out.Pos = float4(In.Pos * 0.1, 1.0);
}
)";

static const char* Shadow_PS = R"(
void main() {
    // Depth-only pass, nothing to output
}
)";

DiligentPBR::DiligentPBR() = default;

DiligentPBR::~DiligentPBR() {
    shutdown();
}

bool DiligentPBR::init(DiligentRenderer& renderer) {
    renderer_ = &renderer;

    if (!createPipelines()) {
        std::cerr << "DiligentPBR: Failed to create pipelines" << std::endl;
        return false;
    }

    if (!createShadowMapResources(2048)) {
        std::cerr << "DiligentPBR: Failed to create shadow map" << std::endl;
        return false;
    }

    initialized_ = true;
    std::cout << "DiligentPBR: Initialized successfully" << std::endl;
    return true;
}

void DiligentPBR::shutdown() {
    // Release Diligent resources (they're reference counted)
    pbrPipeline_ = nullptr;
    shadowPipeline_ = nullptr;
    pbrSRB_ = nullptr;
    shadowSRB_ = nullptr;
    modelConstantsCB_ = nullptr;
    frameConstantsCB_ = nullptr;
    materialCB_ = nullptr;
    lightsCB_ = nullptr;
    shadowConstantsCB_ = nullptr;
    shadowMapTexture_ = nullptr;
    shadowMapDSV_ = nullptr;
    shadowMapSRV_ = nullptr;
    initialized_ = false;
}

bool DiligentPBR::createPipelines() {
    using namespace Diligent;

    auto* device = renderer_->device();
    auto* swapChain = renderer_->swapChain();

    // Create constant buffers
    {
        BufferDesc CBDesc;
        CBDesc.Name = "Model Constants CB";
        CBDesc.Size = sizeof(float) * 32;  // Model (4x4) + NormalMatrix (4x4) = 128 bytes
        CBDesc.Usage = USAGE_DYNAMIC;
        CBDesc.BindFlags = BIND_UNIFORM_BUFFER;
        CBDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        device->CreateBuffer(CBDesc, nullptr, &modelConstantsCB_);
    }
    {
        BufferDesc CBDesc;
        CBDesc.Name = "Frame Constants CB";
        CBDesc.Size = sizeof(float) * 80;  // ViewProj + View + Proj + CameraPos + LightViewProj
        CBDesc.Usage = USAGE_DYNAMIC;
        CBDesc.BindFlags = BIND_UNIFORM_BUFFER;
        CBDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        device->CreateBuffer(CBDesc, nullptr, &frameConstantsCB_);
    }
    {
        BufferDesc CBDesc;
        CBDesc.Name = "Material Constants CB";
        CBDesc.Size = sizeof(float) * 8;
        CBDesc.Usage = USAGE_DYNAMIC;
        CBDesc.BindFlags = BIND_UNIFORM_BUFFER;
        CBDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        device->CreateBuffer(CBDesc, nullptr, &materialCB_);
    }
    {
        BufferDesc CBDesc;
        CBDesc.Name = "Lights Constants CB";
        CBDesc.Size = sizeof(float) * 16;
        CBDesc.Usage = USAGE_DYNAMIC;
        CBDesc.BindFlags = BIND_UNIFORM_BUFFER;
        CBDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        device->CreateBuffer(CBDesc, nullptr, &lightsCB_);
    }
    {
        BufferDesc CBDesc;
        CBDesc.Name = "Shadow Constants CB";
        CBDesc.Size = sizeof(float) * 32;  // LightViewProj (4x4) + Model (4x4) = 128 bytes
        CBDesc.Usage = USAGE_DYNAMIC;
        CBDesc.BindFlags = BIND_UNIFORM_BUFFER;
        CBDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        device->CreateBuffer(CBDesc, nullptr, &shadowConstantsCB_);
    }

    // Create PBR pipeline
    {
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.ShaderCompiler = SHADER_COMPILER_DEFAULT;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
            ShaderCI.Desc.Name = "PBR VS";
            ShaderCI.Source = PBR_VS;
            ShaderCI.EntryPoint = "main";
            device->CreateShader(ShaderCI, &pVS);
            if (!pVS) {
                std::cerr << "DiligentPBR: Failed to create PBR vertex shader" << std::endl;
                return false;
            }
        }

        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
            ShaderCI.Desc.Name = "PBR PS";
            ShaderCI.Source = PBR_PS;
            ShaderCI.EntryPoint = "main";
            device->CreateShader(ShaderCI, &pPS);
            if (!pPS) {
                std::cerr << "DiligentPBR: Failed to create PBR pixel shader" << std::endl;
                return false;
            }
        }

        // Input layout matching Vivid Vertex3D
        // Vertex3D has: position(vec3), normal(vec3), uv(vec2), tangent(vec4) = 48 bytes stride
        constexpr Uint32 Stride = 48;  // sizeof(Vertex3D)
        LayoutElement LayoutElems[] = {
            {0, 0, 3, VT_FLOAT32, False, 0, Stride},   // Position: offset 0
            {1, 0, 3, VT_FLOAT32, False, 12, Stride},  // Normal: offset 12
            {2, 0, 2, VT_FLOAT32, False, 24, Stride},  // UV: offset 24
            // Tangent at offset 32 not used by current shader
        };

        GraphicsPipelineStateCreateInfo PSOCreateInfo;
        PSOCreateInfo.PSODesc.Name = "PBR PSO";
        PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
        PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
        PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = swapChain->GetDesc().ColorBufferFormat;
        PSOCreateInfo.GraphicsPipeline.DSVFormat = TEX_FORMAT_D32_FLOAT;
        PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = True;  // Match OpenGL/WebGPU winding
        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = True;
        PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
        PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);
        PSOCreateInfo.pVS = pVS;
        PSOCreateInfo.pPS = pPS;

        // DEBUG: No resource layout needed for simple pass-through shader
        // When restoring PBR, uncomment the resource layout below
        /*
        ShaderResourceVariableDesc Vars[] = {
            {SHADER_TYPE_VERTEX, "ModelConstants", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
            {SHADER_TYPE_VERTEX, "FrameConstants", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
            {SHADER_TYPE_PIXEL, "FrameConstants", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
            {SHADER_TYPE_PIXEL, "LightConstants", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
            {SHADER_TYPE_PIXEL, "MaterialConstants", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
            {SHADER_TYPE_PIXEL, "ShadowMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
        };
        PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars;
        PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

        // Comparison sampler for shadow mapping
        SamplerDesc CompSamDesc;
        CompSamDesc.ComparisonFunc = COMPARISON_FUNC_LESS;
        CompSamDesc.MinFilter = FILTER_TYPE_COMPARISON_LINEAR;
        CompSamDesc.MagFilter = FILTER_TYPE_COMPARISON_LINEAR;
        CompSamDesc.MipFilter = FILTER_TYPE_COMPARISON_LINEAR;
        CompSamDesc.AddressU = TEXTURE_ADDRESS_BORDER;
        CompSamDesc.AddressV = TEXTURE_ADDRESS_BORDER;
        CompSamDesc.AddressW = TEXTURE_ADDRESS_BORDER;
        CompSamDesc.BorderColor[0] = 1.0f;
        CompSamDesc.BorderColor[1] = 1.0f;
        CompSamDesc.BorderColor[2] = 1.0f;
        CompSamDesc.BorderColor[3] = 1.0f;

        ImmutableSamplerDesc ImtblSamplers[] = {
            {SHADER_TYPE_PIXEL, "ShadowSampler", CompSamDesc}
        };
        PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers = ImtblSamplers;
        PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);
        */

        RefCntAutoPtr<IPipelineState> pPSO;
        device->CreateGraphicsPipelineState(PSOCreateInfo, &pPSO);
        if (!pPSO) {
            std::cerr << "DiligentPBR: Failed to create PBR pipeline" << std::endl;
            return false;
        }
        pbrPipeline_ = pPSO.Detach();

        pbrPipeline_->CreateShaderResourceBinding(&pbrSRB_, true);

        // Bind constant buffers to PBR SRB
        if (auto* pVar = pbrSRB_->GetVariableByName(SHADER_TYPE_VERTEX, "ModelConstants"))
            pVar->Set(modelConstantsCB_);
        if (auto* pVar = pbrSRB_->GetVariableByName(SHADER_TYPE_VERTEX, "FrameConstants"))
            pVar->Set(frameConstantsCB_);
        if (auto* pVar = pbrSRB_->GetVariableByName(SHADER_TYPE_PIXEL, "FrameConstants"))
            pVar->Set(frameConstantsCB_);
        if (auto* pVar = pbrSRB_->GetVariableByName(SHADER_TYPE_PIXEL, "LightConstants"))
            pVar->Set(lightsCB_);
        if (auto* pVar = pbrSRB_->GetVariableByName(SHADER_TYPE_PIXEL, "MaterialConstants"))
            pVar->Set(materialCB_);
    }

    // Create shadow pipeline
    {
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.ShaderCompiler = SHADER_COMPILER_DEFAULT;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
            ShaderCI.Desc.Name = "Shadow VS";
            ShaderCI.Source = Shadow_VS;
            ShaderCI.EntryPoint = "main";
            device->CreateShader(ShaderCI, &pVS);
        }

        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
            ShaderCI.Desc.Name = "Shadow PS";
            ShaderCI.Source = Shadow_PS;
            ShaderCI.EntryPoint = "main";
            device->CreateShader(ShaderCI, &pPS);
        }

        // Shadow pass only needs position, but stride must match Vertex3D (48 bytes)
        constexpr Uint32 ShadowStride = 48;
        LayoutElement LayoutElems[] = {
            {0, 0, 3, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, ShadowStride},  // Position only
        };

        GraphicsPipelineStateCreateInfo PSOCreateInfo;
        PSOCreateInfo.PSODesc.Name = "Shadow PSO";
        PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
        PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 0;
        PSOCreateInfo.GraphicsPipeline.DSVFormat = TEX_FORMAT_D32_FLOAT;
        PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = True;  // Match OpenGL/WebGPU winding
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.DepthBias = 100;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.SlopeScaledDepthBias = 2.0f;
        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = True;
        PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
        PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);
        PSOCreateInfo.pVS = pVS;
        PSOCreateInfo.pPS = pPS;

        // Resource layout for shadow constant buffer
        ShaderResourceVariableDesc ShadowVars[] = {
            {SHADER_TYPE_VERTEX, "ShadowConstants", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
        };
        PSOCreateInfo.PSODesc.ResourceLayout.Variables = ShadowVars;
        PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(ShadowVars);

        RefCntAutoPtr<IPipelineState> pPSO;
        device->CreateGraphicsPipelineState(PSOCreateInfo, &pPSO);
        if (!pPSO) {
            std::cerr << "DiligentPBR: Failed to create shadow pipeline" << std::endl;
            return false;
        }
        shadowPipeline_ = pPSO.Detach();

        shadowPipeline_->CreateShaderResourceBinding(&shadowSRB_, true);

        // Bind constant buffer to shadow SRB
        if (auto* pVar = shadowSRB_->GetVariableByName(SHADER_TYPE_VERTEX, "ShadowConstants"))
            pVar->Set(shadowConstantsCB_);
    }

    return true;
}

bool DiligentPBR::createShadowMapResources(int resolution) {
    using namespace Diligent;

    shadowMapResolution_ = resolution;

    TextureDesc SMDesc;
    SMDesc.Name = "Shadow Map";
    SMDesc.Type = RESOURCE_DIM_TEX_2D;
    SMDesc.Width = resolution;
    SMDesc.Height = resolution;
    SMDesc.Format = TEX_FORMAT_D32_FLOAT;
    SMDesc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
    SMDesc.Usage = USAGE_DEFAULT;
    SMDesc.ClearValue.Format = TEX_FORMAT_D32_FLOAT;
    SMDesc.ClearValue.DepthStencil.Depth = 1.0f;

    RefCntAutoPtr<ITexture> pTexture;
    renderer_->device()->CreateTexture(SMDesc, nullptr, &pTexture);
    if (!pTexture) {
        return false;
    }

    shadowMapTexture_ = pTexture.Detach();
    shadowMapDSV_ = shadowMapTexture_->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    shadowMapSRV_ = shadowMapTexture_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    return true;
}

DiligentMeshData DiligentPBR::createMesh(const std::vector<Vertex3D>& vertices,
                                          const std::vector<uint32_t>& indices) {
    using namespace Diligent;

    DiligentMeshData mesh;
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.indexCount = static_cast<uint32_t>(indices.size());

    // Vertex buffer
    {
        BufferDesc VBDesc;
        VBDesc.Name = "Vertex Buffer";
        VBDesc.Size = vertices.size() * sizeof(Vertex3D);
        VBDesc.BindFlags = BIND_VERTEX_BUFFER;
        VBDesc.Usage = USAGE_IMMUTABLE;

        BufferData VBData;
        VBData.pData = vertices.data();
        VBData.DataSize = VBDesc.Size;

        renderer_->device()->CreateBuffer(VBDesc, &VBData, &mesh.vertexBuffer);
    }

    // Index buffer
    {
        BufferDesc IBDesc;
        IBDesc.Name = "Index Buffer";
        IBDesc.Size = indices.size() * sizeof(uint32_t);
        IBDesc.BindFlags = BIND_INDEX_BUFFER;
        IBDesc.Usage = USAGE_IMMUTABLE;

        BufferData IBData;
        IBData.pData = indices.data();
        IBData.DataSize = IBDesc.Size;

        renderer_->device()->CreateBuffer(IBDesc, &IBData, &mesh.indexBuffer);
    }

    return mesh;
}

void DiligentPBR::destroyMesh(DiligentMeshData& mesh) {
    if (mesh.vertexBuffer) {
        mesh.vertexBuffer->Release();
        mesh.vertexBuffer = nullptr;
    }
    if (mesh.indexBuffer) {
        mesh.indexBuffer->Release();
        mesh.indexBuffer = nullptr;
    }
    mesh.vertexCount = 0;
    mesh.indexCount = 0;
}

void DiligentPBR::render(const DiligentMeshData& mesh,
                         const Camera3D& camera,
                         const glm::mat4& transform,
                         const PBRMaterial& material,
                         const std::vector<DiligentLightData>& lights,
                         Diligent::ITextureView* renderTarget,
                         Diligent::ITextureView* depthTarget,
                         bool clearRT,
                         const glm::vec4& clearColor) {
    using namespace Diligent;

    if (!mesh.vertexBuffer || !mesh.indexBuffer) return;

    auto* ctx = renderer_->context();

    // Set render target
    ctx->SetRenderTargets(1, &renderTarget, depthTarget,
        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set viewport for main rendering (uses renderer dimensions, not shadow map)
    Viewport VP;
    VP.Width = static_cast<float>(renderer_->width());
    VP.Height = static_cast<float>(renderer_->height());
    VP.MinDepth = 0.0f;
    VP.MaxDepth = 1.0f;
    ctx->SetViewports(1, &VP, renderer_->width(), renderer_->height());

    if (clearRT) {
        float clear[4] = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};
        ctx->ClearRenderTarget(renderTarget, clear,
            RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ctx->ClearDepthStencil(depthTarget, CLEAR_DEPTH_FLAG, 1.0f, 0,
            RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    // Update constant buffers
    // Frame constants (b0)
    {
        MapHelper<float> CBData(ctx, frameConstantsCB_, MAP_WRITE, MAP_FLAG_DISCARD);
        float* data = CBData;

        // Build matrices
        glm::mat4 view = glm::lookAt(camera.position, camera.target, camera.up);
        float aspect = static_cast<float>(renderer_->width()) / renderer_->height();
        glm::mat4 proj = glm::perspective(glm::radians(camera.fov), aspect, 0.1f, 1000.0f);
        glm::mat4 viewProj = proj * view;

        // ViewProj
        memcpy(data, &viewProj[0][0], 64);
        data += 16;
        // View
        memcpy(data, &view[0][0], 64);
        data += 16;
        // Proj
        memcpy(data, &proj[0][0], 64);
        data += 16;
        // CameraPos
        memcpy(data, &camera.position[0], 12);
        data += 4;
        // LightViewProj
        memcpy(data, &lightViewProj_[0][0], 64);
    }

    // Model constants (b1) - Model + NormalMatrix
    {
        MapHelper<float> CBData(ctx, modelConstantsCB_, MAP_WRITE, MAP_FLAG_DISCARD);
        float* data = CBData;
        // Model matrix
        memcpy(data, &transform[0][0], 64);
        data += 16;
        // NormalMatrix (transpose of inverse of upper-left 3x3, stored as 4x4)
        glm::mat4 normalMatrix = glm::transpose(glm::inverse(transform));
        memcpy(data, &normalMatrix[0][0], 64);
    }

    // Material constants (b2)
    {
        MapHelper<float> CBData(ctx, materialCB_, MAP_WRITE, MAP_FLAG_DISCARD);
        float* data = CBData;
        data[0] = material.albedo.r;
        data[1] = material.albedo.g;
        data[2] = material.albedo.b;
        data[3] = material.metallic;
        data[4] = material.roughness;
        data[5] = 1.0f;  // AO
        data[6] = 0.0f;
        data[7] = 0.0f;
    }

    // Lights constants (b3)
    {
        MapHelper<float> CBData(ctx, lightsCB_, MAP_WRITE, MAP_FLAG_DISCARD);
        float* data = CBData;

        if (!lights.empty()) {
            const auto& light = lights[0];
            data[0] = light.direction.x;
            data[1] = light.direction.y;
            data[2] = light.direction.z;
            data[3] = light.intensity;
            data[4] = light.color.r;
            data[5] = light.color.g;
            data[6] = light.color.b;
            data[7] = 0.0f;
        }
        // Ambient
        data[8] = 0.1f;
        data[9] = 0.1f;
        data[10] = 0.15f;
        data[11] = 0.2f;
    }

    // Set pipeline and resources
    ctx->SetPipelineState(pbrPipeline_);

    // Bind shadow map
    if (shadowMapSRV_) {
        auto* pVar = pbrSRB_->GetVariableByName(SHADER_TYPE_PIXEL, "ShadowMap");
        if (pVar) {
            pVar->Set(shadowMapSRV_);
        }
    }
    ctx->CommitShaderResources(pbrSRB_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Draw
    IBuffer* pBuffs[] = {mesh.vertexBuffer};
    ctx->SetVertexBuffers(0, 1, pBuffs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ctx->SetIndexBuffer(mesh.indexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawIndexedAttribs DrawAttrs;
    DrawAttrs.IndexType = VT_UINT32;
    DrawAttrs.NumIndices = mesh.indexCount;
    ctx->DrawIndexed(DrawAttrs);
}

void DiligentPBR::beginShadowPass(const DiligentLightData& light,
                                   const glm::vec3& sceneCenter,
                                   float sceneRadius) {
    using namespace Diligent;

    auto* ctx = renderer_->context();

    // Calculate light view-projection matrix
    glm::vec3 lightDir = glm::normalize(light.direction);
    glm::vec3 lightPos = sceneCenter - lightDir * sceneRadius * 2.0f;

    glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, glm::vec3(0, 1, 0));
    glm::mat4 lightProj = glm::ortho(-sceneRadius, sceneRadius,
                                      -sceneRadius, sceneRadius,
                                      0.1f, sceneRadius * 4.0f);
    lightViewProj_ = lightProj * lightView;

    // Set shadow map as render target
    ctx->SetRenderTargets(0, nullptr, shadowMapDSV_,
        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ctx->ClearDepthStencil(shadowMapDSV_, CLEAR_DEPTH_FLAG, 1.0f, 0,
        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set viewport
    Viewport VP;
    VP.Width = static_cast<float>(shadowMapResolution_);
    VP.Height = static_cast<float>(shadowMapResolution_);
    VP.MinDepth = 0.0f;
    VP.MaxDepth = 1.0f;
    ctx->SetViewports(1, &VP, shadowMapResolution_, shadowMapResolution_);

    ctx->SetPipelineState(shadowPipeline_);
}

void DiligentPBR::renderToShadowMap(const DiligentMeshData& mesh,
                                     const glm::mat4& transform) {
    using namespace Diligent;

    if (!mesh.vertexBuffer || !mesh.indexBuffer) return;

    auto* ctx = renderer_->context();

    // Update shadow constants (LightViewProj and Model transform)
    {
        MapHelper<float> CBData(ctx, shadowConstantsCB_, MAP_WRITE, MAP_FLAG_DISCARD);
        float* data = CBData;
        // LightViewProj
        memcpy(data, &lightViewProj_[0][0], 64);
        data += 16;
        // Model
        memcpy(data, &transform[0][0], 64);
    }

    // Commit shadow SRB
    ctx->CommitShaderResources(shadowSRB_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    IBuffer* pBuffs[] = {mesh.vertexBuffer};
    ctx->SetVertexBuffers(0, 1, pBuffs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ctx->SetIndexBuffer(mesh.indexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawIndexedAttribs DrawAttrs;
    DrawAttrs.IndexType = VT_UINT32;
    DrawAttrs.NumIndices = mesh.indexCount;
    ctx->DrawIndexed(DrawAttrs);
}

void DiligentPBR::endShadowPass() {
    // Shadow pass complete
}

Diligent::ITextureView* DiligentPBR::getShadowMapSRV() {
    return shadowMapSRV_;
}

} // namespace vivid

#endif // VIVID_USE_DILIGENT
