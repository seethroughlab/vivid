#pragma once

#include <memory>
#include <string>
#include <functional>

// Forward declarations for GLFW
struct GLFWwindow;

// Forward declarations for Diligent
namespace Diligent {
    struct IRenderDevice;
    struct IDeviceContext;
    struct ISwapChain;
    struct IEngineFactory;
    struct ITextureView;
    class RefCntAutoPtr_Base;

    template<typename T> class RefCntAutoPtr;
}

namespace vivid {

// Configuration for renderer initialization
struct RendererConfig {
    std::string windowTitle = "Vivid";
    int windowWidth = 1280;
    int windowHeight = 720;
    bool vsync = true;
    bool fullscreen = false;
};

// Core renderer class wrapping Diligent Engine
class DiligentRenderer {
public:
    DiligentRenderer();
    ~DiligentRenderer();

    // Non-copyable
    DiligentRenderer(const DiligentRenderer&) = delete;
    DiligentRenderer& operator=(const DiligentRenderer&) = delete;

    // Initialize the renderer with GLFW window and Vulkan backend
    bool initialize(const RendererConfig& config = {});

    // Shutdown and cleanup
    void shutdown();

    // Frame lifecycle
    void beginFrame();
    void endFrame();
    void present();

    // Clear the current render target
    void clear(float r, float g, float b, float a = 1.0f);

    // Window handling
    bool shouldClose() const;
    void pollEvents();
    GLFWwindow* getWindow() const { return m_window; }

    // Window properties
    int getWindowWidth() const { return m_windowWidth; }
    int getWindowHeight() const { return m_windowHeight; }
    float getAspectRatio() const;

    // Resize callback (called automatically, but can be set for custom handling)
    void setResizeCallback(std::function<void(int, int)> callback);

    // Accessors for Diligent objects (for advanced use)
    Diligent::IRenderDevice* getDevice() const;
    Diligent::IDeviceContext* getContext() const;
    Diligent::ISwapChain* getSwapChain() const;
    Diligent::ITextureView* getCurrentRTV() const;
    Diligent::ITextureView* getDepthDSV() const;

    // Frame timing
    double getTime() const;
    double getDeltaTime() const { return m_deltaTime; }
    uint64_t getFrameCount() const { return m_frameCount; }

private:
    // GLFW window
    GLFWwindow* m_window = nullptr;
    int m_windowWidth = 0;
    int m_windowHeight = 0;

    // Diligent objects (using raw pointers internally, managed via RefCntAutoPtr)
    struct DiligentState;
    std::unique_ptr<DiligentState> m_state;

    // Frame timing
    double m_lastFrameTime = 0.0;
    double m_deltaTime = 0.0;
    uint64_t m_frameCount = 0;

    // Resize callback
    std::function<void(int, int)> m_resizeCallback;

    // Internal methods
    bool initGLFW(const RendererConfig& config);
    bool initDiligent();
    void handleResize(int width, int height);

    // GLFW callbacks (static for C callback interface)
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
};

} // namespace vivid
