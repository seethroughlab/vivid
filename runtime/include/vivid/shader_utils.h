#pragma once

#include <string>
#include <memory>

// Include Diligent headers directly - forward declarations don't work well
// with Diligent's template-based RefCntAutoPtr
#include "EngineFactoryVk.h"
#include "Shader.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"

namespace vivid {

using namespace Diligent;

// Configuration for creating a fullscreen effect pipeline
struct FullscreenPipelineDesc {
    std::string name;
    std::string pixelShaderPath;       // Path to fragment/pixel shader file
    std::string pixelShaderSource;     // Or inline source (if path is empty)
    TEXTURE_FORMAT colorFormat = TEX_FORMAT_BGRA8_UNORM_SRGB;
    TEXTURE_FORMAT depthFormat = TEX_FORMAT_D32_FLOAT;
    bool enableBlending = false;
};

// Configuration for creating a 3D mesh rendering pipeline
struct MeshPipelineDesc {
    std::string name;
    std::string vertexShaderPath;      // Path to vertex shader
    std::string pixelShaderPath;       // Path to pixel shader
    TEXTURE_FORMAT colorFormat = TEX_FORMAT_BGRA8_UNORM_SRGB;
    TEXTURE_FORMAT depthFormat = TEX_FORMAT_D32_FLOAT;
    bool enableBlending = false;
    bool depthWrite = true;
    bool depthTest = true;
    CULL_MODE cullMode = CULL_MODE_BACK;
};

// Shader utilities for loading and compiling HLSL shaders
class ShaderUtils {
public:
    ShaderUtils(IRenderDevice* device);
    ~ShaderUtils();

    // Non-copyable
    ShaderUtils(const ShaderUtils&) = delete;
    ShaderUtils& operator=(const ShaderUtils&) = delete;

    // Set the base path for shader files
    void setShaderBasePath(const std::string& path);

    // Load and compile a shader from file
    RefCntAutoPtr<IShader> loadShader(
        const std::string& filePath,
        SHADER_TYPE shaderType,
        const std::string& entryPoint = "main"
    );

    // Compile a shader from source string
    RefCntAutoPtr<IShader> compileShader(
        const std::string& source,
        const std::string& name,
        SHADER_TYPE shaderType,
        const std::string& entryPoint = "main"
    );

    // Get the built-in fullscreen triangle vertex shader
    RefCntAutoPtr<IShader> getFullscreenVS();

    // Create a fullscreen effect pipeline (for 2D texture processing)
    RefCntAutoPtr<IPipelineState> createFullscreenPipeline(
        const FullscreenPipelineDesc& desc
    );

    // Create a 3D mesh rendering pipeline
    RefCntAutoPtr<IPipelineState> createMeshPipeline(
        const MeshPipelineDesc& desc
    );

    // Get the last error message (for debugging)
    const std::string& getLastError() const { return m_lastError; }

    // Check if last operation succeeded
    bool hasError() const { return !m_lastError.empty(); }

private:
    IRenderDevice* m_device;
    std::string m_shaderBasePath;
    std::string m_lastError;

    // Internal state
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    // Helper to configure shader create info
    void configureShaderCI(
        ShaderCreateInfo& ci,
        const std::string& source,
        const std::string& name,
        SHADER_TYPE shaderType,
        const std::string& entryPoint
    );
};

// Built-in fullscreen triangle vertex shader source (HLSL)
// Generates a fullscreen triangle from vertex ID without any vertex buffer
constexpr const char* FULLSCREEN_VS_SOURCE = R"(
struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID) {
    VSOutput output;

    // Generate UV coordinates from vertex ID (0, 1, 2)
    // Vertex 0: (0, 0)  -> (-1, 1)
    // Vertex 1: (2, 0)  -> (3, 1)
    // Vertex 2: (0, 2)  -> (-1, -3)
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);

    // Map UV to clip space
    // Note: Vulkan Y is flipped, so we use -2.0 for Y
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);

    return output;
}
)";

} // namespace vivid
