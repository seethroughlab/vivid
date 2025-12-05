#pragma once

#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "export.h"

// Forward declarations for Diligent types
namespace Diligent {
    struct IRenderDevice;
    struct IDeviceContext;
    struct ISwapChain;
    struct ITexture;
    struct ITextureView;
    struct IPipelineState;
    struct IShaderResourceBinding;
}

struct GLFWwindow;

namespace vivid {

// Forward declarations
class TextureUtils;
class ShaderUtils;
class FullscreenQuad;
class Operator;

/// Information about a registered operator for visualization
struct OperatorInfo {
    std::string name;
    Operator* op = nullptr;
};

/// Context provides access to the rendering system and frame state
class VIVID_API Context {
public:
    Context();
    ~Context();

    // Non-copyable
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    /// Initialize the rendering context with a window
    bool init(int width, int height, const std::string& title);

    /// Shutdown and cleanup resources
    void shutdown();

    /// Begin a new frame
    void beginFrame();

    /// End the current frame and present
    void endFrame();

    /// Check if the window should close
    bool shouldClose() const;

    /// Poll window events
    void pollEvents();

    // --- Time and Frame Info ---

    /// Seconds since start
    float time() const { return time_; }

    /// Delta time (seconds since last frame)
    float dt() const { return dt_; }

    /// Current frame number
    int frame() const { return frame_; }

    // --- Resolution ---

    /// Output width in pixels
    int width() const { return width_; }

    /// Output height in pixels
    int height() const { return height_; }

    // --- Mouse Input ---

    /// Mouse position in pixels
    glm::vec2 mousePosition() const { return mousePos_; }

    /// Mouse position normalized (0-1)
    float mouseNormX() const { return mousePos_.x / static_cast<float>(width_); }
    float mouseNormY() const { return mousePos_.y / static_cast<float>(height_); }

    /// Check if mouse button was pressed this frame
    bool wasMousePressed(int button) const;

    /// Check if mouse button is currently held down
    bool isMouseDown(int button) const;

    /// Get scroll wheel delta since last frame (y is vertical scroll)
    glm::vec2 scrollDelta() const { return scrollDelta_; }

    // --- Keyboard Input ---

    /// Check if a key was pressed this frame (single trigger, not held)
    bool wasKeyPressed(int key) const;

    /// Check if a key is currently held down
    bool isKeyDown(int key) const;

    // --- Diligent Engine Access ---

    Diligent::IRenderDevice* device() const { return device_; }
    Diligent::IDeviceContext* immediateContext() const { return immediateContext_; }
    Diligent::ISwapChain* swapChain() const { return swapChain_; }

    /// Get the current render target view (back buffer)
    Diligent::ITextureView* currentRTV() const;

    /// Get the depth stencil view
    Diligent::ITextureView* currentDSV() const;

    // --- Utility Access ---
    TextureUtils& textureUtils() { return *textureUtils_; }
    ShaderUtils& shaderUtils() { return *shaderUtils_; }
    FullscreenQuad& fullscreenQuad() { return *fullscreenQuad_; }

    // --- Window Access (for addons like ImGui) ---
    GLFWwindow* window() const { return window_; }

    // --- Operator Registration (for visualization) ---

    /// Register an operator for chain visualization
    /// Call this in setup() for each operator you want to visualize
    void registerOperator(const std::string& name, Operator* op);

    /// Get all registered operators
    const std::vector<OperatorInfo>& registeredOperators() const { return operators_; }

    /// Clear all registered operators (called automatically on hot reload)
    void clearRegisteredOperators() { operators_.clear(); }

    /// Enable/disable the chain visualization overlay
    void setVisualizationEnabled(bool enabled) { visualizationEnabled_ = enabled; }
    bool isVisualizationEnabled() const { return visualizationEnabled_; }

private:
    // GLFW window
    GLFWwindow* window_ = nullptr;

    // Diligent Engine objects (raw pointers, ref-counted by Diligent)
    Diligent::IRenderDevice* device_ = nullptr;
    Diligent::IDeviceContext* immediateContext_ = nullptr;
    Diligent::ISwapChain* swapChain_ = nullptr;

    // Frame timing
    float time_ = 0.0f;
    float dt_ = 0.0f;
    int frame_ = 0;
    double lastFrameTime_ = 0.0;

    // Resolution
    int width_ = 0;
    int height_ = 0;

    // Input state
    glm::vec2 mousePos_{0.0f, 0.0f};
    glm::vec2 scrollDelta_{0.0f, 0.0f};
    bool mouseButtons_[8] = {false};
    bool mouseButtonsPressed_[8] = {false};
    bool keys_[512] = {false};        // Current key state
    bool keysPressed_[512] = {false}; // Keys pressed this frame

    // Utilities
    std::unique_ptr<TextureUtils> textureUtils_;
    std::unique_ptr<ShaderUtils> shaderUtils_;
    std::unique_ptr<FullscreenQuad> fullscreenQuad_;

    // Operator registration for visualization
    std::vector<OperatorInfo> operators_;
    bool visualizationEnabled_ = true;

    // GLFW callbacks
    static void onFramebufferResize(GLFWwindow* window, int width, int height);
    static void onMouseMove(GLFWwindow* window, double x, double y);
    static void onMouseButton(GLFWwindow* window, int button, int action, int mods);
    static void onScroll(GLFWwindow* window, double xoffset, double yoffset);
    static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);
};

} // namespace vivid
