// Render3D Operator Implementation

#include "vivid/operators/render3d.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"
#include "vivid/pbr_material.h"
#include "vivid/ibl.h"

#include <iostream>

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"
#include "GraphicsTypesX.hpp"

namespace vivid {

using namespace Diligent;

// Vertex shader for 3D rendering
static const char* Render3D_VS_Source = R"(
cbuffer TransformConstants : register(b0)
{
    float4x4 g_WorldViewProj;
    float4x4 g_World;
    float4x4 g_NormalMatrix;
    float4 g_ObjectColor;
    float4 g_MaterialParams; // x=metallic, y=roughness
};

struct VSInput
{
    float3 position : ATTRIB0;
    float3 normal : ATTRIB1;
    float2 uv : ATTRIB2;
    float3 tangent : ATTRIB3;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 color : TEXCOORD3;
};

void main(in VSInput vs_in, out PSInput ps_in)
{
    ps_in.position = mul(g_WorldViewProj, float4(vs_in.position, 1.0));
    ps_in.worldPos = mul(g_World, float4(vs_in.position, 1.0)).xyz;
    ps_in.normal = normalize(mul((float3x3)g_NormalMatrix, vs_in.normal));
    ps_in.uv = vs_in.uv;
    ps_in.color = g_ObjectColor;
}
)";

// Pixel shader with Cook-Torrance PBR lighting (simple - no textures)
static const char* Render3D_PS_Source = R"(
static const float PI = 3.14159265359;

cbuffer LightConstants : register(b1)
{
    float4 g_CameraPos;
    float4 g_AmbientColor;
    float4 g_LightDir;      // xyz = direction (normalized), w = intensity
    float4 g_LightColor;    // xyz = color, w = unused
    float4 g_MaterialParams; // x=metallic, y=roughness
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 color : TEXCOORD3;
};

// Normal Distribution Function - GGX/Trowbridge-Reitz
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / max(denom, 0.0001);
}

// Geometry Function - Schlick-GGX
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / max(denom, 0.0001);
}

// Geometry Function - Smith's method
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel Function - Schlick's approximation
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float4 main(in PSInput ps_in) : SV_TARGET
{
    float3 N = normalize(ps_in.normal);
    float3 V = normalize(g_CameraPos.xyz - ps_in.worldPos);

    // Material properties
    float3 albedo = ps_in.color.rgb;
    float metallic = g_MaterialParams.x;
    float roughness = max(0.04, g_MaterialParams.y);

    // Calculate F0 (surface reflectance at zero incidence)
    // Dielectrics use 0.04, metals use albedo color
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);

    // Reflectance equation
    float3 Lo = float3(0.0, 0.0, 0.0);

    // Light direction and radiance
    float3 L = normalize(-g_LightDir.xyz);
    float3 H = normalize(V + L);
    float3 radiance = g_LightColor.rgb * g_LightDir.w;

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    // Specular component
    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    float3 specular = numerator / max(denominator, 0.001);

    // Energy conservation: kS + kD = 1
    float3 kS = F;
    float3 kD = float3(1.0, 1.0, 1.0) - kS;
    // Metals have no diffuse component
    kD *= (1.0 - metallic);

    // Outgoing radiance
    float NdotL = max(dot(N, L), 0.0);
    Lo += (kD * albedo / PI + specular) * radiance * NdotL;

    // Ambient lighting (approximation without IBL)
    float3 ambient = g_AmbientColor.rgb * albedo;

    // Final color
    float3 color = ambient + Lo;

    // HDR tone mapping (Reinhard)
    color = color / (color + float3(1.0, 1.0, 1.0));

    // Gamma correction
    color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));

    return float4(color, ps_in.color.a);
}
)";

// Uniform buffer structures
struct TransformConstants {
    glm::mat4 worldViewProj;
    glm::mat4 world;
    glm::mat4 normalMatrix;
    glm::vec4 objectColor;
    glm::vec4 materialParams;
};

struct LightConstants {
    glm::vec4 cameraPos;
    glm::vec4 ambientColor;
    glm::vec4 lightDir;
    glm::vec4 lightColor;
    glm::vec4 materialParams;
};

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

    // Create shaders
    ShaderCreateInfo shaderCI;
    shaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

    RefCntAutoPtr<IShader> vs;
    {
        shaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
        shaderCI.Desc.Name = "Render3D VS";
        shaderCI.EntryPoint = "main";
        shaderCI.Source = Render3D_VS_Source;
        device->CreateShader(shaderCI, &vs);
    }

    RefCntAutoPtr<IShader> ps;
    {
        shaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        shaderCI.Desc.Name = "Render3D PS";
        shaderCI.EntryPoint = "main";
        shaderCI.Source = Render3D_PS_Source;
        device->CreateShader(shaderCI, &ps);
    }

    if (!vs || !ps) return;

    // Create pipeline state
    GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name = "Render3D PSO";
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    psoCI.GraphicsPipeline.NumRenderTargets = 1;
    psoCI.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    psoCI.GraphicsPipeline.DSVFormat = TEX_FORMAT_D32_FLOAT;
    psoCI.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    psoCI.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
    psoCI.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = True;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = True;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS;

    // Input layout for Vertex3D (position: 12, normal: 12, uv: 8, tangent: 12 = 44 bytes total)
    LayoutElement layoutElements[] = {
        // InputIndex, BufferSlot, NumComponents, ValueType, IsNormalized, RelativeOffset, Stride
        {0, 0, 3, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE},  // Position
        {1, 0, 3, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE},  // Normal
        {2, 0, 2, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE},  // UV
        {3, 0, 3, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE},  // Tangent
    };
    psoCI.GraphicsPipeline.InputLayout.LayoutElements = layoutElements;
    psoCI.GraphicsPipeline.InputLayout.NumElements = _countof(layoutElements);

    psoCI.pVS = vs;
    psoCI.pPS = ps;

    // Create uniform buffers first (before PSO) so we can set default resources
    BufferDesc bufDesc;
    bufDesc.Usage = USAGE_DYNAMIC;
    bufDesc.BindFlags = BIND_UNIFORM_BUFFER;
    bufDesc.CPUAccessFlags = CPU_ACCESS_WRITE;

    bufDesc.Name = "Transform Constants";
    bufDesc.Size = sizeof(TransformConstants);
    device->CreateBuffer(bufDesc, nullptr, &transformBuffer_);

    bufDesc.Name = "Light Constants";
    bufDesc.Size = sizeof(LightConstants);
    device->CreateBuffer(bufDesc, nullptr, &lightBuffer_);

    // Define resource layout - make them MUTABLE since we update per-frame
    ShaderResourceVariableDesc variables[] = {
        {SHADER_TYPE_VERTEX, "TransformConstants", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "LightConstants", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };
    psoCI.PSODesc.ResourceLayout.Variables = variables;
    psoCI.PSODesc.ResourceLayout.NumVariables = _countof(variables);

    device->CreateGraphicsPipelineState(psoCI, &pso_);

    if (!pso_) {
        std::cout << "Failed to create Render3D PSO" << std::endl;
        return;
    }

    // Create SRB and bind buffers as mutable variables
    pso_->CreateShaderResourceBinding(&srb_, true);

    if (auto* var = srb_->GetVariableByName(SHADER_TYPE_VERTEX, "TransformConstants")) {
        var->Set(transformBuffer_);
    }
    if (auto* var = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "LightConstants")) {
        var->Set(lightBuffer_);
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

    if (!pso_ || objects_.empty()) return;

    // Set pipeline
    context->SetPipelineState(pso_);

    // Update light constants
    {
        MapHelper<LightConstants> lightData(context, lightBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
        lightData->cameraPos = glm::vec4(camera_.position(), 1.0f);
        lightData->ambientColor = glm::vec4(ambientColor_, 1.0f);

        if (!lights_.empty()) {
            const Light3D& light = lights_[0];
            lightData->lightDir = glm::vec4(light.direction, light.intensity);
            lightData->lightColor = glm::vec4(light.color, 1.0f);
        } else {
            lightData->lightDir = glm::vec4(0, -1, 0, 1);
            lightData->lightColor = glm::vec4(1, 1, 1, 1);
        }
        lightData->materialParams = glm::vec4(0);  // Will be set per-object
    }

    // Get view-projection matrix
    glm::mat4 viewProj = camera_.viewProjectionMatrix();

    // Draw each object
    for (const Object3D& obj : objects_) {
        if (!obj.mesh || !obj.mesh->vertexBuffer()) continue;

        // Update transform constants
        {
            MapHelper<TransformConstants> transformData(context, transformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
            transformData->worldViewProj = viewProj * obj.transform;
            transformData->world = obj.transform;
            transformData->normalMatrix = glm::transpose(glm::inverse(obj.transform));
            transformData->objectColor = obj.color;
            transformData->materialParams = glm::vec4(obj.metallic, obj.roughness, 0, 0);
        }

        // Bind vertex and index buffers
        IBuffer* vertexBuffers[] = {obj.mesh->vertexBuffer()};
        Uint64 offsets[] = {0};
        context->SetVertexBuffers(0, 1, vertexBuffers, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
        context->SetIndexBuffer(obj.mesh->indexBuffer(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Commit resources
        context->CommitShaderResources(srb_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Draw
        DrawIndexedAttribs drawAttrs;
        drawAttrs.IndexType = VT_UINT32;
        drawAttrs.NumIndices = obj.mesh->indexCount();
        drawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
        context->DrawIndexed(drawAttrs);
    }
}

void Render3D::cleanup() {
    if (srb_) { srb_->Release(); srb_ = nullptr; }
    if (pso_) { pso_->Release(); pso_ = nullptr; }
    if (lightBuffer_) { lightBuffer_->Release(); lightBuffer_ = nullptr; }
    if (transformBuffer_) { transformBuffer_->Release(); transformBuffer_ = nullptr; }
    if (depthDSV_) { depthDSV_ = nullptr; }  // View, don't release
    if (depthTexture_) { depthTexture_->Release(); depthTexture_ = nullptr; }
    if (colorSRV_) { colorSRV_ = nullptr; }  // View, don't release
    if (colorRTV_) { colorRTV_ = nullptr; }  // View, don't release
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

} // namespace vivid
