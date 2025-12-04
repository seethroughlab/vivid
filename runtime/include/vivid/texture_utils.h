#pragma once

#include <string>
#include <memory>
#include <unordered_map>

// Include Diligent types for TEXTURE_FORMAT
#include "GraphicsTypes.h"

// Forward declarations for Diligent types
namespace Diligent {
    struct IRenderDevice;
    struct IDeviceContext;
    struct ITexture;
    struct ITextureView;
}

namespace vivid {

/// Managed texture with automatic resource management
struct ManagedTexture {
    Diligent::ITexture* texture = nullptr;
    Diligent::ITextureView* srv = nullptr;  // Shader resource view
    Diligent::ITextureView* rtv = nullptr;  // Render target view (if applicable)
    int width = 0;
    int height = 0;

    bool valid() const { return texture != nullptr; }
    operator bool() const { return valid(); }
};

/// Utility class for texture operations
class TextureUtils {
public:
    TextureUtils(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context);
    ~TextureUtils();

    // Non-copyable
    TextureUtils(const TextureUtils&) = delete;
    TextureUtils& operator=(const TextureUtils&) = delete;

    /// Load a texture from file
    /// @param path Path to image file (PNG, JPG, HDR, etc.)
    /// @param sRGB Whether to treat as sRGB color space
    /// @return Managed texture or empty if failed
    ManagedTexture loadFromFile(const std::string& path, bool sRGB = true);

    /// Load a texture as Texture2DArray (for DiligentFX compatibility)
    /// DiligentFX PBR expects materials as Texture2DArray even with single slice
    /// @param path Path to image file
    /// @param sRGB Whether to treat as sRGB color space
    /// @return Managed texture or empty if failed
    ManagedTexture loadFromFileAsArray(const std::string& path, bool sRGB = true);

    /// Create a render target texture
    /// @param width Width in pixels
    /// @param height Height in pixels
    /// @param format Texture format (default RGBA8)
    /// @return Managed texture with RTV
    ManagedTexture createRenderTarget(int width, int height,
                                       Diligent::TEXTURE_FORMAT format = static_cast<Diligent::TEXTURE_FORMAT>(0));

    /// Create a depth stencil texture
    /// @param width Width in pixels
    /// @param height Height in pixels
    /// @return Managed texture with DSV
    ManagedTexture createDepthStencil(int width, int height);

    /// Release a managed texture
    void release(ManagedTexture& texture);

    /// Clear texture cache
    void clearCache();

private:
    Diligent::IRenderDevice* device_;
    Diligent::IDeviceContext* context_;

    // Texture cache to avoid reloading
    std::unordered_map<std::string, ManagedTexture> cache_;
};

} // namespace vivid
