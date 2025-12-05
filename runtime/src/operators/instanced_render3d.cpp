// InstancedRender3D Operator Implementation
// GPU-instanced rendering for thousands of objects in a single draw call

#include "vivid/operators/instanced_render3d.h"
#include "vivid/pbr_material.h"
#include "vivid/ibl.h"
#include "vivid/context.h"

#include <iostream>
#include <cstring>

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"
#include "GraphicsTypesX.hpp"
#include "RefCntAutoPtr.hpp"
#include "InputLayout.h"
#include "Sampler.h"

namespace vivid {

using namespace Diligent;

// Instanced vertex shader - transforms vertices using per-instance matrix
static const char* InstancedVS_Source = R"(
cbuffer FrameConstants {
    float4x4 g_ViewProj;
    float4 g_LightDir;        // xyz = direction, w = intensity
    float4 g_LightColor;      // rgb = color
    float4 g_AmbientColor;    // rgb = ambient
    float4 g_CameraPos;       // xyz = camera position
    float4 g_MaterialParams;  // x = uvScale, y = useTextures (1.0 or 0.0)
    float4 g_IBLParams;       // x = iblScale, y = useIBL (1.0 or 0.0), z = prefilteredMipLevels
};

struct VSInput {
    // Per-vertex attributes (buffer 0)
    float3 position : ATTRIB0;
    float3 normal   : ATTRIB1;
    float2 uv       : ATTRIB2;

    // Per-instance attributes (buffer 1)
    float4 instRow0      : ATTRIB3;  // Transform matrix row 0
    float4 instRow1      : ATTRIB4;  // Transform matrix row 1
    float4 instRow2      : ATTRIB5;  // Transform matrix row 2
    float4 instRow3      : ATTRIB6;  // Transform matrix row 3
    float4 instColor     : ATTRIB7;  // Instance color (albedo tint)
    float4 instMatProps  : ATTRIB8;  // x=materialIndex, y=metallic, z=roughness, w=unused
};

struct VSOutput {
    float4 position   : SV_POSITION;
    float3 worldPos   : WORLD_POS;
    float3 normal     : NORMAL;
    float2 uv         : TEXCOORD;
    float4 color      : COLOR;
    float3 matProps   : MAT_PROPS;  // materialIndex, metallic, roughness
};

void main(in VSInput input, out VSOutput output) {
    // Reconstruct instance transform matrix from rows
    // MatrixFromRows is defined by Diligent shader system
    float4x4 instanceTransform = MatrixFromRows(
        input.instRow0,
        input.instRow1,
        input.instRow2,
        input.instRow3
    );

    // Transform position
    float4 worldPos = mul(float4(input.position, 1.0), instanceTransform);
    output.worldPos = worldPos.xyz;
    output.position = mul(worldPos, g_ViewProj);

    // Transform normal (using upper 3x3 of instance matrix)
    float3x3 normalMatrix = (float3x3)instanceTransform;
    output.normal = normalize(mul(input.normal, normalMatrix));

    // Pass UV with scale
    output.uv = input.uv * g_MaterialParams.x;

    // Pass instance data
    output.color = input.instColor;
    output.matProps = input.instMatProps.xyz;
}
)";

// PBR pixel shader with metallic-roughness workflow and IBL
static const char* InstancedPS_Source = R"(
cbuffer FrameConstants {
    float4x4 g_ViewProj;
    float4 g_LightDir;        // xyz = direction, w = intensity
    float4 g_LightColor;      // rgb = color
    float4 g_AmbientColor;    // rgb = ambient
    float4 g_CameraPos;       // xyz = camera position
    float4 g_MaterialParams;  // x = uvScale, y = useTextures (1.0 or 0.0)
    float4 g_IBLParams;       // x = iblScale, y = useIBL (1.0 or 0.0), z = prefilteredMipLevels
};

// PBR material textures
Texture2D    g_AlbedoMap;
Texture2D    g_NormalMap;
Texture2D    g_MetallicMap;
Texture2D    g_RoughnessMap;
Texture2D    g_AOMap;
SamplerState g_Sampler;

// IBL textures
TextureCube  g_IrradianceMap;    // Diffuse IBL (pre-convolved)
TextureCube  g_PrefilteredEnvMap; // Specular IBL (pre-filtered, mip-mapped)
Texture2D    g_BRDFLut;          // BRDF integration LUT
SamplerState g_IBLSampler;

struct PSInput {
    float4 position   : SV_POSITION;
    float3 worldPos   : WORLD_POS;
    float3 normal     : NORMAL;
    float2 uv         : TEXCOORD;
    float4 color      : COLOR;
    float3 matProps   : MAT_PROPS;  // materialIndex, metallic, roughness
};

static const float PI = 3.14159265359;

// GGX/Trowbridge-Reitz normal distribution function
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / max(denom, 0.0001);
}

// Schlick-GGX geometry function
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith's geometry function
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

// Fresnel-Schlick with roughness for IBL
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

float4 main(in PSInput input) : SV_TARGET {
    // Sample textures if available
    bool useTextures = g_MaterialParams.y > 0.5;

    float3 albedo;
    float metallic;
    float roughness;
    float ao;
    float3 N;

    if (useTextures) {
        // Sample PBR textures
        float4 albedoSample = g_AlbedoMap.Sample(g_Sampler, input.uv);
        albedo = albedoSample.rgb * input.color.rgb;  // Tint by instance color

        metallic = g_MetallicMap.Sample(g_Sampler, input.uv).r;
        roughness = g_RoughnessMap.Sample(g_Sampler, input.uv).r;
        ao = g_AOMap.Sample(g_Sampler, input.uv).r;

        // Sample and transform normal map
        float3 normalSample = g_NormalMap.Sample(g_Sampler, input.uv).rgb;
        normalSample = normalSample * 2.0 - 1.0;  // Convert from [0,1] to [-1,1]

        // Simple tangent space to world space (approximate for cubes)
        float3 Ng = normalize(input.normal);
        float3 T = normalize(cross(Ng, float3(0, 1, 0)));
        if (length(T) < 0.001) T = normalize(cross(Ng, float3(1, 0, 0)));
        float3 B = cross(Ng, T);
        N = normalize(T * normalSample.x + B * normalSample.y + Ng * normalSample.z);
    } else {
        // Use per-instance properties
        albedo = input.color.rgb;
        metallic = input.matProps.y;
        roughness = input.matProps.z;
        ao = 1.0;
        N = normalize(input.normal);
    }

    roughness = max(roughness, 0.04); // Clamp to avoid divide by zero

    // Vectors
    float3 V = normalize(g_CameraPos.xyz - input.worldPos);
    float3 L = normalize(-g_LightDir.xyz);
    float3 H = normalize(V + L);

    // F0 = base reflectivity (0.04 for dielectrics, albedo for metals)
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    // Specular component
    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    float3 specular = numerator / max(denominator, 0.001);

    // Energy conservation: diffuse = 1 - specular (for dielectrics)
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    // Lambertian diffuse
    float3 diffuse = kD * albedo / PI;

    // Combine with light
    float NdotL = max(dot(N, L), 0.0);
    float3 radiance = g_LightColor.rgb * g_LightDir.w;
    float3 Lo = (diffuse + specular) * radiance * NdotL;

    // IBL ambient lighting
    float3 ambient;
    bool useIBL = g_IBLParams.y > 0.5;

    if (useIBL) {
        float iblScale = g_IBLParams.x;
        float prefilteredMipLevels = g_IBLParams.z;

        // Fresnel with roughness for IBL
        float NdotV = max(dot(N, V), 0.0);
        float3 kS_IBL = FresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kD_IBL = (1.0 - kS_IBL) * (1.0 - metallic);

        // Diffuse IBL from irradiance map
        float3 irradiance = g_IrradianceMap.Sample(g_IBLSampler, N).rgb;
        float3 diffuseIBL = irradiance * albedo;

        // Specular IBL from prefiltered environment map
        float3 R = reflect(-V, N);
        float mipLevel = roughness * prefilteredMipLevels;
        float3 prefilteredColor = g_PrefilteredEnvMap.SampleLevel(g_IBLSampler, R, mipLevel).rgb;

        // BRDF integration lookup
        float2 brdfUV = float2(NdotV, roughness);
        float2 envBRDF = g_BRDFLut.Sample(g_IBLSampler, brdfUV).rg;
        float3 specularIBL = prefilteredColor * (kS_IBL * envBRDF.x + envBRDF.y);

        // Combine IBL components
        ambient = (kD_IBL * diffuseIBL + specularIBL) * ao * iblScale;
    } else {
        // Fallback to simple ambient
        ambient = g_AmbientColor.rgb * albedo * ao * (1.0 - metallic * 0.5);
    }

    // Final color with tone mapping
    float3 color = ambient + Lo;

    // Simple Reinhard tone mapping
    color = color / (color + 1.0);

    // Gamma correction
    color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));

    return float4(color, input.color.a);
}
)";

// Frame constants structure (must match HLSL cbuffer)
struct FrameConstants {
    float viewProj[16];      // 4x4 matrix
    float lightDir[4];       // xyz = dir, w = intensity
    float lightColor[4];     // rgb = color
    float ambientColor[4];   // rgb = ambient
    float cameraPos[4];      // xyz = camera position
    float materialParams[4]; // x = uvScale, y = useTextures (1.0 or 0.0)
    float iblParams[4];      // x = iblScale, y = useIBL (1.0 or 0.0), z = prefilteredMipLevels
};

// Instance data structure for GPU (must match vertex attributes)
// This is laid out for row-major matrix storage
struct InstanceGPU {
    float row0[4];       // Transform row 0
    float row1[4];       // Transform row 1
    float row2[4];       // Transform row 2
    float row3[4];       // Transform row 3
    float color[4];      // RGBA color
    float materialProps[4]; // materialIndex, metallic, roughness, padding
};

InstancedRender3D::InstancedRender3D() {
    // Default light
    light_.direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));
    light_.color = glm::vec3(1.0f, 1.0f, 1.0f);
    light_.intensity = 1.0f;
}

InstancedRender3D::~InstancedRender3D() {
    cleanup();
}

void InstancedRender3D::init(Context& ctx) {
    // Cache device and context for later use
    device_ = ctx.device();
    context_ = ctx.immediateContext();

    createRenderTargets(ctx);
    createPipeline(ctx);
}

void InstancedRender3D::createRenderTargets(Context& ctx) {
    outputWidth_ = ctx.width();
    outputHeight_ = ctx.height();

    auto* device = ctx.device();

    // Create color render target
    TextureDesc colorDesc;
    colorDesc.Name = "InstancedRender3D Color";
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
    depthDesc.Name = "InstancedRender3D Depth";
    depthDesc.Type = RESOURCE_DIM_TEX_2D;
    depthDesc.Width = outputWidth_;
    depthDesc.Height = outputHeight_;
    depthDesc.Format = TEX_FORMAT_D32_FLOAT;
    depthDesc.BindFlags = BIND_DEPTH_STENCIL;

    device->CreateTexture(depthDesc, nullptr, &depthTexture_);
    depthDSV_ = depthTexture_->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
}

void InstancedRender3D::createPipeline(Context& ctx) {
    auto* device = ctx.device();

    // Create vertex shader
    ShaderCreateInfo vsCI;
    vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
    vsCI.Desc.Name = "Instanced VS";
    vsCI.EntryPoint = "main";
    vsCI.Source = InstancedVS_Source;

    RefCntAutoPtr<IShader> vs;
    device->CreateShader(vsCI, &vs);
    if (!vs) {
        std::cerr << "Failed to create instanced vertex shader" << std::endl;
        return;
    }

    // Create pixel shader
    ShaderCreateInfo psCI;
    psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
    psCI.Desc.Name = "Instanced PS";
    psCI.EntryPoint = "main";
    psCI.Source = InstancedPS_Source;

    RefCntAutoPtr<IShader> ps;
    device->CreateShader(psCI, &ps);
    if (!ps) {
        std::cerr << "Failed to create instanced pixel shader" << std::endl;
        return;
    }

    // Create pipeline state
    GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name = "Instanced Render PSO";
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    psoCI.pVS = vs;
    psoCI.pPS = ps;

    // Input layout: per-vertex data (buffer 0) + per-instance data (buffer 1)
    // Vertex3D has: position(3), normal(3), uv(2), tangent(3) = 44 bytes
    constexpr Uint32 vertexStride = sizeof(Vertex3D);  // 44 bytes
    constexpr Uint32 instanceStride = sizeof(InstanceGPU);  // 96 bytes (6 * 16)

    LayoutElement layoutElements[] = {
        // Per-vertex attributes (buffer 0)
        // LayoutElement(InputIndex, BufferSlot, NumComponents, ValueType, IsNormalized, RelativeOffset, Stride, Frequency, InstanceDataStepRate)
        {0, 0, 3, VT_FLOAT32, False, 0, vertexStride, INPUT_ELEMENT_FREQUENCY_PER_VERTEX, 0},   // Position
        {1, 0, 3, VT_FLOAT32, False, 12, vertexStride, INPUT_ELEMENT_FREQUENCY_PER_VERTEX, 0},  // Normal
        {2, 0, 2, VT_FLOAT32, False, 24, vertexStride, INPUT_ELEMENT_FREQUENCY_PER_VERTEX, 0},  // UV

        // Per-instance attributes (buffer 1)
        {3, 1, 4, VT_FLOAT32, False, 0, instanceStride, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1},   // Row 0
        {4, 1, 4, VT_FLOAT32, False, 16, instanceStride, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1},  // Row 1
        {5, 1, 4, VT_FLOAT32, False, 32, instanceStride, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1},  // Row 2
        {6, 1, 4, VT_FLOAT32, False, 48, instanceStride, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1},  // Row 3
        {7, 1, 4, VT_FLOAT32, False, 64, instanceStride, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1},  // Color
        {8, 1, 4, VT_FLOAT32, False, 80, instanceStride, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1},  // Material props
    };
    constexpr Uint32 numLayoutElements = 9;
    psoCI.GraphicsPipeline.InputLayout.LayoutElements = layoutElements;
    psoCI.GraphicsPipeline.InputLayout.NumElements = numLayoutElements;

    // Render target
    psoCI.GraphicsPipeline.NumRenderTargets = 1;
    psoCI.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    psoCI.GraphicsPipeline.DSVFormat = TEX_FORMAT_D32_FLOAT;

    // Depth and rasterizer state
    psoCI.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    psoCI.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
    psoCI.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = True;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = True;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS;

    // Resource layout - use MUTABLE for resources that change per-frame
    ShaderResourceVariableDesc vars[] = {
        {SHADER_TYPE_VERTEX, "FrameConstants", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "FrameConstants", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        // PBR material textures
        {SHADER_TYPE_PIXEL, "g_AlbedoMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "g_NormalMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "g_MetallicMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "g_RoughnessMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "g_AOMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        // IBL textures
        {SHADER_TYPE_PIXEL, "g_IrradianceMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "g_PrefilteredEnvMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "g_BRDFLut", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };
    psoCI.PSODesc.ResourceLayout.Variables = vars;
    psoCI.PSODesc.ResourceLayout.NumVariables = 10;

    // Immutable samplers for textures
    SamplerDesc samplerDesc;
    samplerDesc.MinFilter = FILTER_TYPE_LINEAR;
    samplerDesc.MagFilter = FILTER_TYPE_LINEAR;
    samplerDesc.MipFilter = FILTER_TYPE_LINEAR;
    samplerDesc.AddressU = TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = TEXTURE_ADDRESS_WRAP;

    SamplerDesc iblSamplerDesc;
    iblSamplerDesc.MinFilter = FILTER_TYPE_LINEAR;
    iblSamplerDesc.MagFilter = FILTER_TYPE_LINEAR;
    iblSamplerDesc.MipFilter = FILTER_TYPE_LINEAR;
    iblSamplerDesc.AddressU = TEXTURE_ADDRESS_CLAMP;
    iblSamplerDesc.AddressV = TEXTURE_ADDRESS_CLAMP;
    iblSamplerDesc.AddressW = TEXTURE_ADDRESS_CLAMP;

    ImmutableSamplerDesc immutableSamplers[] = {
        {SHADER_TYPE_PIXEL, "g_Sampler", samplerDesc},
        {SHADER_TYPE_PIXEL, "g_IBLSampler", iblSamplerDesc}
    };
    psoCI.PSODesc.ResourceLayout.ImmutableSamplers = immutableSamplers;
    psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = 2;

    device->CreateGraphicsPipelineState(psoCI, &pso_);
    if (!pso_) {
        std::cerr << "Failed to create instanced render PSO" << std::endl;
        return;
    }

    // Create constants buffer
    BufferDesc bufDesc;
    bufDesc.Name = "Instanced Frame Constants";
    bufDesc.Usage = USAGE_DYNAMIC;
    bufDesc.BindFlags = BIND_UNIFORM_BUFFER;
    bufDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
    bufDesc.Size = sizeof(FrameConstants);
    device->CreateBuffer(bufDesc, nullptr, &frameConstantsBuffer_);

    // Create and bind SRB
    pso_->CreateShaderResourceBinding(&srb_, true);
    if (srb_) {
        if (auto* var = srb_->GetVariableByName(SHADER_TYPE_VERTEX, "FrameConstants")) {
            var->Set(frameConstantsBuffer_);
        }
        if (auto* var = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "FrameConstants")) {
            var->Set(frameConstantsBuffer_);
        }
    }

    std::cout << "Created InstancedRender3D pipeline" << std::endl;
}

void InstancedRender3D::process(Context& ctx) {
    camera_.setAspectRatio(static_cast<float>(ctx.width()) / static_cast<float>(ctx.height()));
    renderScene(ctx);
}

void InstancedRender3D::renderScene(Context& ctx) {
    if (!pso_ || !mesh_ || instanceCount_ == 0) return;

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

    // Update frame constants
    {
        MapHelper<FrameConstants> constants(context, frameConstantsBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
        if (constants) {
            // View-projection matrix (transpose for HLSL row-major)
            glm::mat4 view = camera_.viewMatrix();
            glm::mat4 proj = camera_.projectionMatrix();
            glm::mat4 viewProj = proj * view;

            // Transpose for HLSL (GLM is column-major, HLSL expects row-major)
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    constants->viewProj[row * 4 + col] = viewProj[col][row];
                }
            }

            // Light direction (normalized)
            glm::vec3 lightDir = glm::normalize(light_.direction);
            constants->lightDir[0] = lightDir.x;
            constants->lightDir[1] = lightDir.y;
            constants->lightDir[2] = lightDir.z;
            constants->lightDir[3] = light_.intensity;

            // Light color
            constants->lightColor[0] = light_.color.r;
            constants->lightColor[1] = light_.color.g;
            constants->lightColor[2] = light_.color.b;
            constants->lightColor[3] = 1.0f;

            // Ambient color
            constants->ambientColor[0] = ambientColor_.r;
            constants->ambientColor[1] = ambientColor_.g;
            constants->ambientColor[2] = ambientColor_.b;
            constants->ambientColor[3] = 1.0f;

            // Camera position
            glm::vec3 camPos = camera_.position();
            constants->cameraPos[0] = camPos.x;
            constants->cameraPos[1] = camPos.y;
            constants->cameraPos[2] = camPos.z;
            constants->cameraPos[3] = 1.0f;

            // Material params
            constants->materialParams[0] = uvScale_;
            constants->materialParams[1] = (material_ && material_->hasAlbedo()) ? 1.0f : 0.0f;
            constants->materialParams[2] = 0.0f;
            constants->materialParams[3] = 0.0f;

            // IBL params
            bool hasIBL = environment_ && environment_->isLoaded();
            constants->iblParams[0] = iblScale_;
            constants->iblParams[1] = hasIBL ? 1.0f : 0.0f;
            constants->iblParams[2] = hasIBL ? 7.0f : 0.0f;  // prefilteredMipLevels (typical value)
            constants->iblParams[3] = 0.0f;
        }
    }

    // Bind textures if we have a material
    if (material_ && srb_) {
        if (auto* var = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_AlbedoMap")) {
            var->Set(material_->albedoSRV());
        }
        if (auto* var = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_NormalMap")) {
            var->Set(material_->normalSRV());
        }
        if (auto* var = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_MetallicMap")) {
            var->Set(material_->metallicSRV());
        }
        if (auto* var = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_RoughnessMap")) {
            var->Set(material_->roughnessSRV());
        }
        if (auto* var = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_AOMap")) {
            var->Set(material_->aoSRV());
        }
    }

    // Bind IBL textures if we have an environment
    if (environment_ && environment_->isLoaded() && srb_) {
        if (auto* var = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceMap")) {
            var->Set(environment_->irradianceSRV());
        }
        if (auto* var = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_PrefilteredEnvMap")) {
            var->Set(environment_->prefilteredSRV());
        }
        if (auto* var = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_BRDFLut")) {
            var->Set(environment_->brdfLutSRV());
        }
    }

    // Set pipeline and resources
    context->SetPipelineState(pso_);
    context->CommitShaderResources(srb_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Bind vertex buffers
    IBuffer* vertexBuffers[] = {mesh_->vertexBuffer(), instanceBuffer_};
    Uint64 offsets[] = {0, 0};
    context->SetVertexBuffers(0, 2, vertexBuffers, offsets,
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                              SET_VERTEX_BUFFERS_FLAG_RESET);
    context->SetIndexBuffer(mesh_->indexBuffer(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Draw instanced
    DrawIndexedAttribs drawAttrs;
    drawAttrs.IndexType = VT_UINT32;
    drawAttrs.NumIndices = mesh_->indexCount();
    drawAttrs.NumInstances = static_cast<Uint32>(instanceCount_);
    drawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
    context->DrawIndexed(drawAttrs);
}

void InstancedRender3D::cleanup() {
    if (srb_) { srb_->Release(); srb_ = nullptr; }
    if (frameConstantsBuffer_) { frameConstantsBuffer_->Release(); frameConstantsBuffer_ = nullptr; }
    if (pso_) { pso_->Release(); pso_ = nullptr; }
    if (instanceBuffer_) { instanceBuffer_->Release(); instanceBuffer_ = nullptr; }
    if (depthDSV_) { depthDSV_ = nullptr; }
    if (depthTexture_) { depthTexture_->Release(); depthTexture_ = nullptr; }
    if (colorSRV_) { colorSRV_ = nullptr; }
    if (colorRTV_) { colorRTV_ = nullptr; }
    if (colorTexture_) { colorTexture_->Release(); colorTexture_ = nullptr; }
}

ITextureView* InstancedRender3D::getOutputSRV() {
    return colorSRV_;
}

ITextureView* InstancedRender3D::getOutputRTV() {
    return colorRTV_;
}

InstancedRender3D& InstancedRender3D::setMesh(Mesh* mesh) {
    mesh_ = mesh;
    return *this;
}

InstancedRender3D& InstancedRender3D::setInstances(const std::vector<Instance3D>& instances) {
    instanceCount_ = instances.size();
    if (instanceCount_ == 0) return *this;

    // Ensure we have valid device and context
    if (!device_ || !context_) return *this;

    // Check if we need to resize the buffer
    if (instanceCount_ > instanceBufferCapacity_) {
        if (instanceBuffer_) {
            instanceBuffer_->Release();
            instanceBuffer_ = nullptr;
        }

        // Allocate with some headroom
        instanceBufferCapacity_ = instanceCount_ + (instanceCount_ / 2);

        BufferDesc bufDesc;
        bufDesc.Name = "Instance Buffer";
        bufDesc.Usage = USAGE_DYNAMIC;
        bufDesc.BindFlags = BIND_VERTEX_BUFFER;
        bufDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        bufDesc.Size = static_cast<Uint64>(instanceBufferCapacity_ * sizeof(InstanceGPU));
        device_->CreateBuffer(bufDesc, nullptr, &instanceBuffer_);
    }

    if (!instanceBuffer_) return *this;

    // Upload instance data
    MapHelper<InstanceGPU> gpuData(context_, instanceBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    if (gpuData) {
        for (size_t i = 0; i < instanceCount_; ++i) {
            const Instance3D& src = instances[i];
            InstanceGPU& dst = gpuData[i];

            // Copy GLM columns as rows (GLM column-major -> HLSL row interpretation)
            // GLM m[col][row], we copy each column as a row for the shader
            const glm::mat4& m = src.transform;
            // Column 0 -> row 0
            dst.row0[0] = m[0][0]; dst.row0[1] = m[0][1]; dst.row0[2] = m[0][2]; dst.row0[3] = m[0][3];
            // Column 1 -> row 1
            dst.row1[0] = m[1][0]; dst.row1[1] = m[1][1]; dst.row1[2] = m[1][2]; dst.row1[3] = m[1][3];
            // Column 2 -> row 2
            dst.row2[0] = m[2][0]; dst.row2[1] = m[2][1]; dst.row2[2] = m[2][2]; dst.row2[3] = m[2][3];
            // Column 3 -> row 3
            dst.row3[0] = m[3][0]; dst.row3[1] = m[3][1]; dst.row3[2] = m[3][2]; dst.row3[3] = m[3][3];

            dst.color[0] = src.color.r;
            dst.color[1] = src.color.g;
            dst.color[2] = src.color.b;
            dst.color[3] = src.color.a;

            // Material properties
            dst.materialProps[0] = src.materialIndex;
            dst.materialProps[1] = src.metallic;
            dst.materialProps[2] = src.roughness;
            dst.materialProps[3] = 0.0f;  // padding
        }
    }

    return *this;
}

InstancedRender3D& InstancedRender3D::clearInstances() {
    instanceCount_ = 0;
    return *this;
}

InstancedRender3D& InstancedRender3D::backgroundColor(float r, float g, float b, float a) {
    backgroundColor_ = glm::vec4(r, g, b, a);
    return *this;
}

InstancedRender3D& InstancedRender3D::backgroundColor(const glm::vec4& color) {
    backgroundColor_ = color;
    return *this;
}

InstancedRender3D& InstancedRender3D::ambientColor(float r, float g, float b) {
    ambientColor_ = glm::vec3(r, g, b);
    return *this;
}

InstancedRender3D& InstancedRender3D::ambientColor(const glm::vec3& color) {
    ambientColor_ = color;
    return *this;
}

InstancedRender3D& InstancedRender3D::setLight(const InstancedLight& light) {
    light_ = light;
    light_.direction = glm::normalize(light_.direction);
    return *this;
}

InstancedRender3D& InstancedRender3D::setMaterial(PBRMaterial* material) {
    material_ = material;
    return *this;
}

InstancedRender3D& InstancedRender3D::setEnvironment(IBLEnvironment* env) {
    environment_ = env;
    return *this;
}

} // namespace vivid
