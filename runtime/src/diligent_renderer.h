#pragma once

// Diligent-based renderer that mirrors the Renderer interface
// This allows switching between WebGPU and Diligent backends

#ifdef VIVID_USE_DILIGENT

#include <vivid/types.h>
#include "diligent_backend.h"

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "RefCntAutoPtr.hpp"
#include "BasicMath.hpp"

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

struct GLFWwindow;

namespace vivid {

// Internal texture data for Diligent
struct DiligentTextureData {
    Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> view;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> rtv;  // Render target view
};

class DiligentRenderer {
public:
    DiligentRenderer();
    ~DiligentRenderer();

    // Non-copyable
    DiligentRenderer(const DiligentRenderer&) = delete;
    DiligentRenderer& operator=(const DiligentRenderer&) = delete;

    // Initialize with a GLFW window
    bool init(GLFWwindow* window, int width, int height);
    void shutdown();

    // Frame lifecycle
    bool beginFrame();
    void endFrame();

    // Clear the current frame
    void clear(float r, float g, float b, float a = 1.0f);

    // Texture management
    Texture createTexture(int width, int height);
    void destroyTexture(Texture& texture);

    // Upload pixel data to a texture
    void uploadTexturePixels(Texture& texture, const uint8_t* pixels, int width, int height);

    // Blit a texture to the screen
    void blitToScreen(const Texture& texture);

    // Fill a texture with a solid color
    void fillTexture(Texture& texture, float r, float g, float b, float a = 1.0f);

    // Read pixel data from a texture
    std::vector<uint8_t> readTexturePixels(const Texture& texture);

    // Resize handling
    void resize(int width, int height);

    // VSync control
    void setVSync(bool enabled);
    bool vsyncEnabled() const { return vsync_; }

    // Accessors
    int width() const { return width_; }
    int height() const { return height_; }
    bool isInitialized() const { return backend_.isValid(); }

    // Diligent accessors
    Diligent::IRenderDevice* device() { return backend_.device(); }
    Diligent::IDeviceContext* context() { return backend_.context(); }
    Diligent::ISwapChain* swapChain() { return backend_.swapChain(); }

    // Present the swap chain
    void present() { backend_.swapChain()->Present(); }

private:
    bool createBlitPipeline();

    DiligentBackend backend_;

    // Blit pipeline for presenting textures to screen
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> blitPipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> blitSRB_;
    Diligent::RefCntAutoPtr<Diligent::ISampler> blitSampler_;

    int width_ = 0;
    int height_ = 0;
    bool vsync_ = true;
};

} // namespace vivid

#endif // VIVID_USE_DILIGENT
