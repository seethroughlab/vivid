#pragma once

// Diligent Engine backend for Vivid
// This class abstracts Diligent's rendering API for use in the Vivid runtime

#ifdef VIVID_USE_DILIGENT

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "EngineFactory.h"
#include "RefCntAutoPtr.hpp"

#include <glm/glm.hpp>

namespace vivid {

class DiligentBackend {
public:
    DiligentBackend() = default;
    ~DiligentBackend();

    // Initialization
    bool init(void* nativeWindow, int width, int height);
    void shutdown();

    // Window management
    void resize(int width, int height);
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    // Frame lifecycle
    void beginFrame();
    void endFrame();
    void present();

    // Clear operations
    void clear(const glm::vec4& color);
    void clearDepth(float depth = 1.0f);

    // Accessors for Diligent objects
    Diligent::IRenderDevice* device() { return device_.RawPtr(); }
    Diligent::IDeviceContext* context() { return immediateContext_.RawPtr(); }
    Diligent::ISwapChain* swapChain() { return swapChain_.RawPtr(); }

    // Backend info
    const char* getBackendName() const;
    bool isValid() const { return device_ != nullptr; }

private:
    // Core Diligent objects
    Diligent::RefCntAutoPtr<Diligent::IEngineFactory> engineFactory_;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> immediateContext_;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapChain_;

    // Cached state
    int width_ = 0;
    int height_ = 0;
    void* nativeWindow_ = nullptr;
};

} // namespace vivid

#endif // VIVID_USE_DILIGENT
