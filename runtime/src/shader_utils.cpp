#include "vivid/shader_utils.h"

// Diligent Engine headers - EngineFactoryVk.h includes core headers
#include "EngineFactoryVk.h"
#include "Shader.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"
#include "BasicFileStream.hpp"
#include "DefaultShaderSourceStreamFactory.h"

#include <fstream>
#include <sstream>
#include <iostream>

namespace vivid {

using namespace Diligent;

struct ShaderUtils::Impl {
    RefCntAutoPtr<IShaderSourceInputStreamFactory> streamFactory;
    RefCntAutoPtr<IShader> fullscreenVS;
};

ShaderUtils::ShaderUtils(IRenderDevice* device)
    : m_device(device)
    , m_impl(std::make_unique<Impl>()) {

    // Create default shader source stream factory
    IShaderSourceInputStreamFactory* pFactory = nullptr;
    CreateDefaultShaderSourceStreamFactory("shaders", &pFactory);
    m_impl->streamFactory.Attach(pFactory);
}

ShaderUtils::~ShaderUtils() = default;

void ShaderUtils::setShaderBasePath(const std::string& path) {
    m_shaderBasePath = path;
    // Recreate stream factory with new path
    IShaderSourceInputStreamFactory* pFactory = nullptr;
    CreateDefaultShaderSourceStreamFactory(path.c_str(), &pFactory);
    m_impl->streamFactory.Attach(pFactory);
}

void ShaderUtils::configureShaderCI(
    ShaderCreateInfo& ci,
    const std::string& source,
    const std::string& name,
    SHADER_TYPE shaderType,
    const std::string& entryPoint
) {
    ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    ci.Desc.ShaderType = shaderType;
    ci.Desc.Name = name.c_str();
    ci.EntryPoint = entryPoint.c_str();
    ci.Source = source.c_str();
    ci.pShaderSourceStreamFactory = m_impl->streamFactory;

    // Use Web GPU compatible SPIRV for better cross-platform support
    ci.ShaderCompiler = SHADER_COMPILER_DEFAULT;

    // Enable row major matrices (HLSL default)
    ci.HLSLVersion = {6, 0};
    ci.GLSLVersion = {460, 0};
}

RefCntAutoPtr<IShader> ShaderUtils::loadShader(
    const std::string& filePath,
    SHADER_TYPE shaderType,
    const std::string& entryPoint
) {
    m_lastError.clear();

    // Build full path
    std::string fullPath = m_shaderBasePath.empty() ? filePath : (m_shaderBasePath + "/" + filePath);

    // Read file contents
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        m_lastError = "Failed to open shader file: " + fullPath;
        std::cerr << "[ShaderUtils] " << m_lastError << std::endl;
        return {};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    // Extract name from path
    std::string name = filePath;
    size_t lastSlash = name.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        name = name.substr(lastSlash + 1);
    }

    return compileShader(source, name, shaderType, entryPoint);
}

RefCntAutoPtr<IShader> ShaderUtils::compileShader(
    const std::string& source,
    const std::string& name,
    SHADER_TYPE shaderType,
    const std::string& entryPoint
) {
    m_lastError.clear();

    ShaderCreateInfo ci;
    configureShaderCI(ci, source, name, shaderType, entryPoint);

    RefCntAutoPtr<IShader> shader;
    m_device->CreateShader(ci, &shader);

    if (!shader) {
        m_lastError = "Failed to compile shader: " + name;
        std::cerr << "[ShaderUtils] " << m_lastError << std::endl;
        return {};
    }

    std::cout << "[ShaderUtils] Compiled shader: " << name << std::endl;
    return shader;
}

RefCntAutoPtr<IShader> ShaderUtils::getFullscreenVS() {
    if (!m_impl->fullscreenVS) {
        m_impl->fullscreenVS = compileShader(
            FULLSCREEN_VS_SOURCE,
            "FullscreenVS",
            SHADER_TYPE_VERTEX,
            "main"
        );
    }
    return m_impl->fullscreenVS;
}

RefCntAutoPtr<IPipelineState> ShaderUtils::createFullscreenPipeline(
    const FullscreenPipelineDesc& desc
) {
    m_lastError.clear();

    // Get or create the fullscreen vertex shader
    auto vs = getFullscreenVS();
    if (!vs) {
        m_lastError = "Failed to get fullscreen vertex shader";
        return {};
    }

    // Load or compile the pixel shader
    RefCntAutoPtr<IShader> ps;
    if (!desc.pixelShaderPath.empty()) {
        ps = loadShader(desc.pixelShaderPath, SHADER_TYPE_PIXEL, "main");
    } else if (!desc.pixelShaderSource.empty()) {
        ps = compileShader(desc.pixelShaderSource, desc.name + "_PS", SHADER_TYPE_PIXEL, "main");
    } else {
        m_lastError = "No pixel shader source provided for pipeline: " + desc.name;
        return {};
    }

    if (!ps) {
        return {};  // Error already set by loadShader/compileShader
    }

    // Create pipeline state
    GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name = desc.name.c_str();
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    // Set default variable type to MUTABLE for textures/samplers
    // This allows binding through SRB instead of static pipeline resources
    psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

    psoCI.GraphicsPipeline.NumRenderTargets = 1;
    psoCI.GraphicsPipeline.RTVFormats[0] = desc.colorFormat;
    psoCI.GraphicsPipeline.DSVFormat = desc.depthFormat;

    // Fullscreen triangle - no input layout needed, uses SV_VertexID
    psoCI.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Rasterizer state - no culling for fullscreen triangle
    psoCI.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    psoCI.GraphicsPipeline.RasterizerDesc.FillMode = FILL_MODE_SOLID;

    // Depth stencil - disabled for 2D effects
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = False;

    // Blending
    if (desc.enableBlending) {
        auto& rt0 = psoCI.GraphicsPipeline.BlendDesc.RenderTargets[0];
        rt0.BlendEnable = True;
        rt0.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
        rt0.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
        rt0.BlendOp = BLEND_OPERATION_ADD;
        rt0.SrcBlendAlpha = BLEND_FACTOR_ONE;
        rt0.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
        rt0.BlendOpAlpha = BLEND_OPERATION_ADD;
    }

    // Shaders
    psoCI.pVS = vs;
    psoCI.pPS = ps;

    RefCntAutoPtr<IPipelineState> pso;
    m_device->CreateGraphicsPipelineState(psoCI, &pso);

    if (!pso) {
        m_lastError = "Failed to create pipeline state: " + desc.name;
        std::cerr << "[ShaderUtils] " << m_lastError << std::endl;
        return {};
    }

    std::cout << "[ShaderUtils] Created pipeline: " << desc.name << std::endl;
    return pso;
}

RefCntAutoPtr<IPipelineState> ShaderUtils::createMeshPipeline(
    const MeshPipelineDesc& desc
) {
    m_lastError.clear();

    // Load vertex shader
    auto vs = loadShader(desc.vertexShaderPath, SHADER_TYPE_VERTEX, "VSMain");
    if (!vs) {
        return {};
    }

    // Load pixel shader
    auto ps = loadShader(desc.pixelShaderPath, SHADER_TYPE_PIXEL, "PSMain");
    if (!ps) {
        return {};
    }

    // Vertex input layout matching Vertex3D struct
    // struct Vertex3D { vec3 position; vec3 normal; vec2 uv; vec4 tangent; }
    LayoutElement layoutElements[] = {
        // Position (vec3)
        LayoutElement{0, 0, 3, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE, INPUT_ELEMENT_FREQUENCY_PER_VERTEX},
        // Normal (vec3)
        LayoutElement{1, 0, 3, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE, INPUT_ELEMENT_FREQUENCY_PER_VERTEX},
        // UV (vec2)
        LayoutElement{2, 0, 2, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE, INPUT_ELEMENT_FREQUENCY_PER_VERTEX},
        // Tangent (vec4)
        LayoutElement{3, 0, 4, VT_FLOAT32, False, LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE, INPUT_ELEMENT_FREQUENCY_PER_VERTEX},
    };

    // Create pipeline state
    GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name = desc.name.c_str();
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    // Set default variable type to MUTABLE for uniform buffers and textures
    psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

    psoCI.GraphicsPipeline.NumRenderTargets = 1;
    psoCI.GraphicsPipeline.RTVFormats[0] = desc.colorFormat;
    psoCI.GraphicsPipeline.DSVFormat = desc.depthFormat;

    // Vertex input layout
    psoCI.GraphicsPipeline.InputLayout.LayoutElements = layoutElements;
    psoCI.GraphicsPipeline.InputLayout.NumElements = _countof(layoutElements);

    psoCI.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Rasterizer state
    psoCI.GraphicsPipeline.RasterizerDesc.CullMode = desc.cullMode;
    psoCI.GraphicsPipeline.RasterizerDesc.FillMode = FILL_MODE_SOLID;
    psoCI.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = False;  // CW for LH coordinate system

    // Depth stencil
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthEnable = desc.depthTest ? True : False;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = desc.depthWrite ? True : False;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS;

    // Blending
    if (desc.enableBlending) {
        auto& rt0 = psoCI.GraphicsPipeline.BlendDesc.RenderTargets[0];
        rt0.BlendEnable = True;
        rt0.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
        rt0.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
        rt0.BlendOp = BLEND_OPERATION_ADD;
        rt0.SrcBlendAlpha = BLEND_FACTOR_ONE;
        rt0.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
        rt0.BlendOpAlpha = BLEND_OPERATION_ADD;
    }

    // Shaders
    psoCI.pVS = vs;
    psoCI.pPS = ps;

    RefCntAutoPtr<IPipelineState> pso;
    m_device->CreateGraphicsPipelineState(psoCI, &pso);

    if (!pso) {
        m_lastError = "Failed to create mesh pipeline state: " + desc.name;
        std::cerr << "[ShaderUtils] " << m_lastError << std::endl;
        return {};
    }

    std::cout << "[ShaderUtils] Created mesh pipeline: " << desc.name << std::endl;
    return pso;
}

} // namespace vivid
