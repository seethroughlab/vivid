#pragma once

#include <string>
#include <unordered_map>
#include <memory>

#include "GraphicsTypes.h"

// Diligent forward declarations
namespace Diligent {
    struct IRenderDevice;
    struct IDeviceContext;
    struct IShader;
    struct IPipelineState;
    struct IShaderResourceBinding;
    struct IBuffer;
}

namespace vivid {

/// Utility class for shader operations
class ShaderUtils {
public:
    ShaderUtils(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context);
    ~ShaderUtils();

    // Non-copyable
    ShaderUtils(const ShaderUtils&) = delete;
    ShaderUtils& operator=(const ShaderUtils&) = delete;

    /// Load and compile a shader from file
    /// @param path Path to HLSL shader file
    /// @param entryPoint Entry point function name
    /// @param shaderType Type of shader (vertex, pixel, etc.)
    /// @return Compiled shader or nullptr on failure
    Diligent::IShader* loadShader(const std::string& path,
                                   const std::string& entryPoint,
                                   Diligent::SHADER_TYPE shaderType);

    /// Load shader from source string
    Diligent::IShader* loadShaderFromSource(const std::string& source,
                                             const std::string& name,
                                             const std::string& entryPoint,
                                             Diligent::SHADER_TYPE shaderType);

    /// Create a fullscreen effect pipeline (vertex + pixel shader)
    /// Returns PSO for rendering fullscreen quad with the given pixel shader
    Diligent::IPipelineState* createFullscreenPipeline(
        const std::string& name,
        Diligent::IShader* pixelShader,
        bool hasInputTexture = true);

    /// Create an output pipeline that renders to the swap chain
    /// Uses the specified render target format (should match swap chain)
    Diligent::IPipelineState* createOutputPipeline(
        const std::string& name,
        Diligent::IShader* pixelShader,
        Diligent::TEXTURE_FORMAT rtFormat);

    /// Get the built-in fullscreen vertex shader
    Diligent::IShader* getFullscreenVS();

    /// Clear shader cache
    void clearCache();

private:
    Diligent::IRenderDevice* device_;
    Diligent::IDeviceContext* context_;

    // Cached shaders
    std::unordered_map<std::string, Diligent::IShader*> shaderCache_;
    Diligent::IShader* fullscreenVS_ = nullptr;
};

/// Manages fullscreen quad rendering for 2D effects
class FullscreenQuad {
public:
    FullscreenQuad(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context);
    ~FullscreenQuad();

    /// Draw a fullscreen triangle (more efficient than quad)
    void draw();

private:
    Diligent::IDeviceContext* context_;
};

} // namespace vivid
