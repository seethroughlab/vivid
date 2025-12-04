// Vivid Shader Utilities Implementation

#include "vivid/shader_utils.h"

#include "GraphicsTypes.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Shader.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>

namespace vivid {

using namespace Diligent;

// Fullscreen triangle vertex shader - generates vertices procedurally
static const char* FullscreenVS_Source = R"(
struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

void main(in uint vertexId : SV_VertexID,
          out VSOutput output)
{
    // Generate fullscreen triangle vertices
    // Vertex 0: (-1, -1), Vertex 1: (3, -1), Vertex 2: (-1, 3)
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
    // Flip Y for correct texture orientation
    output.uv.y = 1.0 - output.uv.y;
}
)";

ShaderUtils::ShaderUtils(IRenderDevice* device, IDeviceContext* context)
    : device_(device)
    , context_(context)
{
}

ShaderUtils::~ShaderUtils() {
    clearCache();
    if (fullscreenVS_) {
        fullscreenVS_->Release();
        fullscreenVS_ = nullptr;
    }
}

IShader* ShaderUtils::loadShader(const std::string& path,
                                  const std::string& entryPoint,
                                  SHADER_TYPE shaderType) {
    // Check cache
    std::string cacheKey = path + ":" + entryPoint;
    auto it = shaderCache_.find(cacheKey);
    if (it != shaderCache_.end()) {
        return it->second;
    }

    // Read file
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << path << std::endl;
        return nullptr;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    IShader* shader = loadShaderFromSource(source, path, entryPoint, shaderType);
    if (shader) {
        shaderCache_[cacheKey] = shader;
    }
    return shader;
}

IShader* ShaderUtils::loadShaderFromSource(const std::string& source,
                                            const std::string& name,
                                            const std::string& entryPoint,
                                            SHADER_TYPE shaderType) {
    ShaderCreateInfo shaderCI;
    shaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shaderCI.Desc.ShaderType = shaderType;
    shaderCI.Desc.Name = name.c_str();
    shaderCI.EntryPoint = entryPoint.c_str();
    shaderCI.Source = source.c_str();

    // Use default shader compiler
    shaderCI.ShaderCompiler = SHADER_COMPILER_DEFAULT;

    RefCntAutoPtr<IShader> shader;
    device_->CreateShader(shaderCI, &shader);

    if (!shader) {
        std::cerr << "Failed to compile shader: " << name << std::endl;
        return nullptr;
    }

    return shader.Detach();
}

IShader* ShaderUtils::getFullscreenVS() {
    if (!fullscreenVS_) {
        fullscreenVS_ = loadShaderFromSource(
            FullscreenVS_Source,
            "FullscreenVS",
            "main",
            SHADER_TYPE_VERTEX
        );
    }
    return fullscreenVS_;
}

IPipelineState* ShaderUtils::createFullscreenPipeline(
    const std::string& name,
    IShader* pixelShader,
    bool hasInputTexture) {

    IShader* vs = getFullscreenVS();
    if (!vs || !pixelShader) {
        return nullptr;
    }

    GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name = name.c_str();
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    psoCI.pVS = vs;
    psoCI.pPS = pixelShader;

    // No vertex input - we generate vertices in the shader
    psoCI.GraphicsPipeline.InputLayout.NumElements = 0;

    // Primitive topology
    psoCI.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Render target format - use BGRA which is preferred on macOS
    psoCI.GraphicsPipeline.NumRenderTargets = 1;
    psoCI.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_BGRA8_UNORM_SRGB;

    // No depth testing for 2D effects
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;

    // No culling for fullscreen triangle
    psoCI.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;

    // Default blending (opaque)
    psoCI.GraphicsPipeline.BlendDesc.RenderTargets[0].BlendEnable = false;

    // Resource layout - make all shader resources dynamic for flexibility
    std::vector<ShaderResourceVariableDesc> vars;

    // Always add Constants uniform buffer as dynamic
    vars.push_back({SHADER_TYPE_PIXEL, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});

    if (hasInputTexture) {
        vars.push_back({SHADER_TYPE_PIXEL, "g_Texture", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
    }

    psoCI.PSODesc.ResourceLayout.Variables = vars.data();
    psoCI.PSODesc.ResourceLayout.NumVariables = static_cast<Uint32>(vars.size());

    // Sampler for textures
    SamplerDesc samplerDesc;
    samplerDesc.MinFilter = FILTER_TYPE_LINEAR;
    samplerDesc.MagFilter = FILTER_TYPE_LINEAR;
    samplerDesc.MipFilter = FILTER_TYPE_LINEAR;
    samplerDesc.AddressU = TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = TEXTURE_ADDRESS_CLAMP;

    ImmutableSamplerDesc immutableSamplers[] = {
        {SHADER_TYPE_PIXEL, "g_Sampler", samplerDesc},
        {SHADER_TYPE_PIXEL, "g_Texture", samplerDesc}  // Also assign sampler to g_Texture
    };
    psoCI.PSODesc.ResourceLayout.ImmutableSamplers = immutableSamplers;
    psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = 2;

    RefCntAutoPtr<IPipelineState> pso;
    device_->CreateGraphicsPipelineState(psoCI, &pso);

    if (!pso) {
        std::cerr << "Failed to create pipeline: " << name << std::endl;
        return nullptr;
    }

    return pso.Detach();
}

IPipelineState* ShaderUtils::createOutputPipeline(
    const std::string& name,
    IShader* pixelShader,
    TEXTURE_FORMAT rtFormat) {

    IShader* vs = getFullscreenVS();
    if (!vs || !pixelShader) {
        return nullptr;
    }

    GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name = name.c_str();
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    psoCI.pVS = vs;
    psoCI.pPS = pixelShader;

    // No vertex input - we generate vertices in the shader
    psoCI.GraphicsPipeline.InputLayout.NumElements = 0;

    // Primitive topology
    psoCI.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Use the swap chain's render target format
    psoCI.GraphicsPipeline.NumRenderTargets = 1;
    psoCI.GraphicsPipeline.RTVFormats[0] = rtFormat;

    // No depth testing for output - we render directly to swap chain
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    psoCI.GraphicsPipeline.DSVFormat = TEX_FORMAT_UNKNOWN;

    // No culling for fullscreen triangle
    psoCI.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;

    // Default blending (opaque)
    psoCI.GraphicsPipeline.BlendDesc.RenderTargets[0].BlendEnable = false;

    // Resource layout - texture is dynamic
    ShaderResourceVariableDesc vars[] = {
        {SHADER_TYPE_PIXEL, "g_Texture", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
    };
    psoCI.PSODesc.ResourceLayout.Variables = vars;
    psoCI.PSODesc.ResourceLayout.NumVariables = 1;

    // Sampler for input texture
    SamplerDesc samplerDesc;
    samplerDesc.MinFilter = FILTER_TYPE_LINEAR;
    samplerDesc.MagFilter = FILTER_TYPE_LINEAR;
    samplerDesc.MipFilter = FILTER_TYPE_LINEAR;
    samplerDesc.AddressU = TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = TEXTURE_ADDRESS_CLAMP;

    ImmutableSamplerDesc immutableSamplers[] = {
        {SHADER_TYPE_PIXEL, "g_Sampler", samplerDesc},
        {SHADER_TYPE_PIXEL, "g_Texture", samplerDesc}
    };
    psoCI.PSODesc.ResourceLayout.ImmutableSamplers = immutableSamplers;
    psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = 2;

    RefCntAutoPtr<IPipelineState> pso;
    device_->CreateGraphicsPipelineState(psoCI, &pso);

    if (!pso) {
        std::cerr << "Failed to create output pipeline: " << name << std::endl;
        return nullptr;
    }

    return pso.Detach();
}

void ShaderUtils::clearCache() {
    for (auto& [key, shader] : shaderCache_) {
        if (shader) {
            shader->Release();
        }
    }
    shaderCache_.clear();
}

// FullscreenQuad implementation
FullscreenQuad::FullscreenQuad(IRenderDevice* device, IDeviceContext* context)
    : context_(context)
{
    // No vertex buffer needed - vertices generated in shader
}

FullscreenQuad::~FullscreenQuad() {
}

void FullscreenQuad::draw() {
    // Draw 3 vertices (fullscreen triangle)
    DrawAttribs drawAttribs;
    drawAttribs.NumVertices = 3;
    drawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;
    context_->Draw(drawAttribs);
}

} // namespace vivid
