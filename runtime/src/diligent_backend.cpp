#include "diligent_backend.h"

#ifdef VIVID_USE_DILIGENT

#include "EngineFactoryVk.h"
#include <iostream>

#if PLATFORM_MACOS
#include <TargetConditionals.h>
#endif

namespace vivid {

DiligentBackend::~DiligentBackend() {
    shutdown();
}

bool DiligentBackend::init(void* nativeWindow, int width, int height) {
    using namespace Diligent;

    nativeWindow_ = nativeWindow;
    width_ = width;
    height_ = height;

    // Get Vulkan engine factory
    auto* pFactoryVk = GetEngineFactoryVk();
    if (!pFactoryVk) {
        std::cerr << "DiligentBackend: Failed to get Vulkan engine factory" << std::endl;
        return false;
    }

    engineFactory_ = pFactoryVk;

    // Configure engine
    EngineVkCreateInfo engineCI;
    engineCI.EnableValidation = true;  // Enable for debug builds

    // Create device and context
    pFactoryVk->CreateDeviceAndContextsVk(engineCI, &device_, &immediateContext_);

    if (!device_) {
        std::cerr << "DiligentBackend: Failed to create Vulkan device" << std::endl;
        return false;
    }

    // Create swap chain
    SwapChainDesc SCDesc;
    SCDesc.Width = width;
    SCDesc.Height = height;
    SCDesc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM_SRGB;
    SCDesc.DepthBufferFormat = TEX_FORMAT_D32_FLOAT;

#if PLATFORM_WIN32
    Win32NativeWindow Window{static_cast<HWND>(nativeWindow)};
#elif PLATFORM_LINUX
    LinuxNativeWindow Window;
    Window.WindowId = reinterpret_cast<Uint32>(nativeWindow);
    // Note: Display and other X11/Wayland params would need to be set
#elif PLATFORM_MACOS
    MacOSNativeWindow Window;
    Window.pNSView = nativeWindow;
#else
    #error "Unsupported platform"
#endif

    pFactoryVk->CreateSwapChainVk(device_, immediateContext_, SCDesc, Window, &swapChain_);

    if (!swapChain_) {
        std::cerr << "DiligentBackend: Failed to create swap chain" << std::endl;
        return false;
    }

    std::cout << "DiligentBackend: Initialized with Vulkan backend" << std::endl;
    const auto& deviceInfo = device_->GetDeviceInfo();
    std::cout << "  API Version: " << deviceInfo.APIVersion.Major << "."
              << deviceInfo.APIVersion.Minor << std::endl;

    return true;
}

void DiligentBackend::shutdown() {
    swapChain_.Release();
    immediateContext_.Release();
    device_.Release();
    engineFactory_.Release();

    width_ = 0;
    height_ = 0;
    nativeWindow_ = nullptr;
}

void DiligentBackend::resize(int width, int height) {
    if (width == width_ && height == height_) {
        return;
    }

    width_ = width;
    height_ = height;

    if (swapChain_) {
        swapChain_->Resize(width, height);
    }
}

void DiligentBackend::beginFrame() {
    // Nothing special needed at frame start for Vulkan
}

void DiligentBackend::endFrame() {
    // Nothing special needed at frame end for Vulkan
}

void DiligentBackend::present() {
    if (swapChain_) {
        swapChain_->Present();
    }
}

void DiligentBackend::clear(const glm::vec4& color) {
    if (!swapChain_ || !immediateContext_) {
        return;
    }

    auto* pRTV = swapChain_->GetCurrentBackBufferRTV();
    auto* pDSV = swapChain_->GetDepthBufferDSV();

    float clearColor[4] = {color.r, color.g, color.b, color.a};

    immediateContext_->SetRenderTargets(1, &pRTV, pDSV,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    immediateContext_->ClearRenderTarget(pRTV, clearColor,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void DiligentBackend::clearDepth(float depth) {
    if (!swapChain_ || !immediateContext_) {
        return;
    }

    auto* pDSV = swapChain_->GetDepthBufferDSV();
    immediateContext_->ClearDepthStencil(pDSV,
        Diligent::CLEAR_DEPTH_FLAG, depth, 0,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

const char* DiligentBackend::getBackendName() const {
    return "Vulkan";
}

} // namespace vivid

#endif // VIVID_USE_DILIGENT
