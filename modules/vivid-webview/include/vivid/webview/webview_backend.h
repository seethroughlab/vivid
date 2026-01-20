#pragma once

// Vivid WebView - Platform Backend Interface
// Abstract interface for platform-specific WebView implementations

#include <vivid/context.h>
#include <webgpu/webgpu.h>
#include <string>
#include <functional>

namespace vivid::webview {

/**
 * @brief Mouse button identifiers for input events
 */
enum class MouseButton {
    Left = 0,
    Right = 1,
    Middle = 2
};

/**
 * @brief Mouse event types
 */
enum class MouseEventType {
    Move,
    Down,
    Up,
    Scroll
};

/**
 * @brief Key event types
 */
enum class KeyEventType {
    Down,
    Up,
    Char  // Character input (for text fields)
};

/**
 * @brief Keyboard modifier flags
 */
struct KeyModifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool meta = false;  // Command on macOS, Windows key on Windows
};

/**
 * @brief Abstract interface for platform-specific WebView backends.
 *
 * Each platform (macOS, Windows, Linux) provides its own implementation:
 * - macOS: WKWebView with IOSurface GPU-direct texture sharing
 * - Windows: WebView2 with DXGI texture sharing
 * - Linux: CEF with offscreen rendering
 *
 * The backend is responsible for:
 * 1. Creating and managing the native webview
 * 2. Rendering content to a GPU texture accessible by WGPU
 * 3. Forwarding input events to the webview
 * 4. Executing JavaScript and receiving callbacks
 */
class WebViewBackend {
public:
    virtual ~WebViewBackend() = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Initialize the backend with GPU context
     * @param ctx Vivid context with device/queue
     * @param width Initial width in pixels
     * @param height Initial height in pixels
     * @return true if initialization succeeded
     */
    virtual bool init(Context& ctx, int width, int height) = 0;

    /**
     * @brief Update the webview and render to texture
     * @param ctx Vivid context
     * @return true if a new frame was rendered
     */
    virtual bool update(Context& ctx) = 0;

    /**
     * @brief Clean up resources
     */
    virtual void cleanup() = 0;

    // -------------------------------------------------------------------------
    // Content Loading
    // -------------------------------------------------------------------------

    /**
     * @brief Navigate to URL
     */
    virtual void loadUrl(const std::string& url) = 0;

    /**
     * @brief Load HTML content directly
     */
    virtual void loadHtml(const std::string& html, const std::string& baseUrl) = 0;

    /**
     * @brief Reload current page
     */
    virtual void reload() = 0;

    /**
     * @brief Stop loading
     */
    virtual void stop() = 0;

    /**
     * @brief Navigate back
     */
    virtual void goBack() = 0;

    /**
     * @brief Navigate forward
     */
    virtual void goForward() = 0;

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /**
     * @brief Resize the webview
     */
    virtual void resize(int width, int height) = 0;

    /**
     * @brief Set transparent background
     */
    virtual void setTransparent(bool transparent) = 0;

    /**
     * @brief Enable/disable JavaScript
     */
    virtual void setJavaScriptEnabled(bool enabled) = 0;

    /**
     * @brief Set zoom level
     */
    virtual void setZoom(float zoom) = 0;

    // -------------------------------------------------------------------------
    // JavaScript Interop
    // -------------------------------------------------------------------------

    /**
     * @brief Execute JavaScript code
     * @param script JavaScript to execute
     * @param callback Optional callback with result as JSON string
     */
    virtual void executeJS(const std::string& script,
                          std::function<void(const std::string&)> callback) = 0;

    /**
     * @brief Register a callback invokable from JavaScript
     * @param name Function name (accessible as window.vivid.name)
     * @param callback Function to call with JSON arguments
     */
    virtual void registerCallback(const std::string& name,
                                  std::function<void(const std::string&)> callback) = 0;

    // -------------------------------------------------------------------------
    // Input Events
    // -------------------------------------------------------------------------

    /**
     * @brief Send mouse event to webview
     * @param type Event type (move, down, up, scroll)
     * @param x X position in webview coordinates
     * @param y Y position in webview coordinates
     * @param button Which button (for down/up events)
     * @param scrollDeltaX Horizontal scroll delta (for scroll events)
     * @param scrollDeltaY Vertical scroll delta (for scroll events)
     * @param modifiers Active keyboard modifiers
     */
    virtual void sendMouseEvent(MouseEventType type, float x, float y,
                                MouseButton button = MouseButton::Left,
                                float scrollDeltaX = 0, float scrollDeltaY = 0,
                                KeyModifiers modifiers = {}) = 0;

    /**
     * @brief Send keyboard event to webview
     * @param type Event type (down, up, char)
     * @param keyCode Platform-independent key code (GLFW key codes)
     * @param scanCode Platform-specific scan code
     * @param character Unicode character (for Char events)
     * @param modifiers Active keyboard modifiers
     */
    virtual void sendKeyEvent(KeyEventType type, int keyCode, int scanCode,
                             uint32_t character = 0,
                             KeyModifiers modifiers = {}) = 0;

    /**
     * @brief Set focus state
     */
    virtual void setFocus(bool focused) = 0;

    // -------------------------------------------------------------------------
    // State Queries
    // -------------------------------------------------------------------------

    [[nodiscard]] virtual bool isLoading() const = 0;
    [[nodiscard]] virtual bool isReady() const = 0;
    [[nodiscard]] virtual std::string currentUrl() const = 0;
    [[nodiscard]] virtual std::string pageTitle() const = 0;
    [[nodiscard]] virtual bool canGoBack() const = 0;
    [[nodiscard]] virtual bool canGoForward() const = 0;

    // -------------------------------------------------------------------------
    // GPU Texture Access
    // -------------------------------------------------------------------------

    /**
     * @brief Get the rendered texture
     */
    [[nodiscard]] virtual WGPUTexture texture() const = 0;

    /**
     * @brief Get the texture view
     */
    [[nodiscard]] virtual WGPUTextureView textureView() const = 0;

    /**
     * @brief Get current width
     */
    [[nodiscard]] virtual int width() const = 0;

    /**
     * @brief Get current height
     */
    [[nodiscard]] virtual int height() const = 0;
};

/**
 * @brief Create the platform-appropriate WebView backend
 * @return Unique pointer to backend, or nullptr if platform unsupported
 */
std::unique_ptr<WebViewBackend> createWebViewBackend();

} // namespace vivid::webview
