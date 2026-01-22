#pragma once

// Vivid WebView - WebView Operator
// Renders interactive web content (HTML/CSS/JS/WebGL) to texture
// Platform support:
//   - macOS: WKWebView with IOSurface GPU-direct texture sharing
//   - Windows: WebView2 (stub - not yet implemented)
//   - Linux: CEF (stub - not yet implemented)

#include <vivid/effects/texture_operator.h>
#include <vivid/webview/export.h>
#include <vivid/operator_registry.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <functional>
#include <vector>

namespace vivid::webview {

// Forward declarations for platform backends
class WebViewBackend;

/**
 * @brief Input state for WebView when Context's state has been blocked
 *
 * Use this to pass raw input state to WebView when the Context's mouse
 * state has been zeroed (e.g., when visualizer is visible and blockMouseInput
 * was called). The WebView will calculate transitions internally.
 */
struct RawInputState {
    float mouseX = 0;
    float mouseY = 0;
    bool mouseButtons[3] = {false, false, false};  // Left, Right, Middle
    float scrollX = 0;
    float scrollY = 0;
    bool shiftHeld = false;
    bool ctrlHeld = false;
    bool altHeld = false;
    bool superHeld = false;
    // Keyboard state (from GLFW key codes)
    bool keyDown[512] = {};
    // Character input for text entry
    std::vector<uint32_t> characterInput;
};

/**
 * @brief WebView operator for rendering web content to texture.
 *
 * Renders HTML/CSS/JS content (including WebGL/Three.js) to a GPU texture
 * for use in Vivid chains. Supports transparency for UI overlays and
 * interactive input (mouse/keyboard forwarding).
 *
 * Usage:
 *   auto& web = chain.add<WebView>("ui");
 *   web.setUrl("file://assets/ui/overlay.html");
 *   web.setTransparent(true);
 *   web.setSize(1920, 1080);
 *
 *   // Composite over scene
 *   auto& blend = chain.add<Blend>("composite");
 *   blend.input("scene");
 *   blend.overlay("ui");
 *
 * For WebGL/Three.js content:
 *   auto& web = chain.add<WebView>("threejs");
 *   web.setUrl("file://assets/webgl/scene.html");
 *   web.setSize(1920, 1080);
 *   chain.output("threejs");
 */
class VIVID_WEBVIEW_API WebView : public effects::TextureOperator {
public:
    WebView();
    ~WebView() override;

    // Non-copyable
    WebView(const WebView&) = delete;
    WebView& operator=(const WebView&) = delete;

    // -------------------------------------------------------------------------
    // Configuration API
    // -------------------------------------------------------------------------

    /**
     * @brief Set the URL to load
     * @param url URL (http://, https://, or file:// for local content)
     *
     * For local HTML files, use file:// URLs relative to project assets:
     *   web.setUrl("file://assets/ui/overlay.html");
     */
    void setUrl(const std::string& url);

    /**
     * @brief Load HTML content directly from string
     * @param html HTML content
     * @param baseUrl Optional base URL for relative paths
     */
    void loadHtml(const std::string& html, const std::string& baseUrl = "");

    /**
     * @brief Set the webview render size in pixels
     */
    void setSize(int width, int height);

    /**
     * @brief Enable transparent background for UI overlays
     * @param transparent If true, webview background is transparent
     */
    void setTransparent(bool transparent);

    /**
     * @brief Enable/disable JavaScript execution
     */
    void setJavaScriptEnabled(bool enabled);

    /**
     * @brief Set zoom level (1.0 = 100%)
     */
    void setZoom(float zoom);

    /**
     * @brief Enable/disable input forwarding (mouse/keyboard)
     * @param enabled If true, Vivid forwards input events to webview
     */
    void setInputEnabled(bool enabled);

    /**
     * @brief Set input offset for positioned WebViews
     * @param x X offset from window origin to WebView top-left
     * @param y Y offset from window origin to WebView top-left
     *
     * When the WebView is rendered at a position other than (0,0),
     * set this offset so mouse coordinates are properly translated.
     */
    void setInputOffset(int x, int y);

    /**
     * @brief Set frame rate limit for webview rendering
     * @param fps Target frames per second (0 = match Vivid frame rate)
     */
    void setFrameRate(int fps);

    // -------------------------------------------------------------------------
    // JavaScript Interop
    // -------------------------------------------------------------------------

    /**
     * @brief Execute JavaScript in the webview
     * @param script JavaScript code to execute
     * @param callback Optional callback with result (as JSON string)
     */
    void executeJS(const std::string& script,
                   std::function<void(const std::string&)> callback = nullptr);

    /**
     * @brief Register a callback that JavaScript can invoke
     * @param name Function name accessible from JS as window.vivid.name()
     * @param callback C++ function to call with JSON arguments
     *
     * Example:
     *   web.registerCallback("onButtonClick", [](const std::string& args) {
     *       // args is JSON: {"buttonId": "start"}
     *   });
     *
     * Then in JavaScript:
     *   window.vivid.onButtonClick(JSON.stringify({buttonId: "start"}));
     */
    void registerCallback(const std::string& name,
                          std::function<void(const std::string&)> callback);

    // -------------------------------------------------------------------------
    // State Queries
    // -------------------------------------------------------------------------

    [[nodiscard]] bool isLoading() const;
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] const std::string& currentUrl() const;
    [[nodiscard]] const std::string& pageTitle() const;

    [[nodiscard]] int webviewWidth() const;
    [[nodiscard]] int webviewHeight() const;

    // -------------------------------------------------------------------------
    // Focus Management
    // -------------------------------------------------------------------------

    /**
     * @brief Check if this WebView currently has keyboard focus
     * @return True if this WebView will receive keyboard/character input
     */
    [[nodiscard]] bool hasFocus() const { return s_focusedWebView == this; }

    /**
     * @brief Request keyboard focus for this WebView
     *
     * Only one WebView can have focus at a time. Calling this will
     * unfocus any previously focused WebView.
     */
    void requestFocus();

    /**
     * @brief Release keyboard focus from this WebView
     */
    void releaseFocus();

    /**
     * @brief Get the currently focused WebView (if any)
     * @return Pointer to focused WebView, or nullptr if none
     */
    static WebView* focusedWebView() { return s_focusedWebView; }

    // -------------------------------------------------------------------------
    // Navigation
    // -------------------------------------------------------------------------

    void reload();
    void goBack();
    void goForward();
    void stop();

    // -------------------------------------------------------------------------
    // Operator Interface
    // -------------------------------------------------------------------------

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "WebView"; }

    /**
     * @brief Process with raw input state (bypasses Context's mouse state)
     *
     * Use this when Context's mouse state has been blocked (e.g., when
     * visualizer is visible). Pass input read directly from GLFW.
     */
    void processWithRawInput(Context& ctx, const RawInputState& input);

    // Override to return texture from backend
    WGPUTextureView outputView() const override { return m_activeView; }
    WGPUTexture outputTexture() const override { return m_activeTexture; }

    std::vector<ParamDecl> params() override {
        return {
            {"zoom", ParamType::Float, 0.25f, 4.0f, {m_zoom, 0, 0, 0}},
            {"transparent", ParamType::Float, 0.0f, 1.0f, {m_transparent ? 1.0f : 0.0f, 0, 0, 0}},
            {"inputEnabled", ParamType::Float, 0.0f, 1.0f, {m_inputEnabled ? 1.0f : 0.0f, 0, 0, 0}}
        };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "zoom") { out[0] = m_zoom; return true; }
        if (name == "transparent") { out[0] = m_transparent ? 1.0f : 0.0f; return true; }
        if (name == "inputEnabled") { out[0] = m_inputEnabled ? 1.0f : 0.0f; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "zoom") { setZoom(value[0]); return true; }
        if (name == "transparent") { setTransparent(value[0] > 0.5f); return true; }
        if (name == "inputEnabled") { setInputEnabled(value[0] > 0.5f); return true; }
        return false;
    }

private:
    void createFallbackTexture(Context& ctx);
    void handleInputEvents(Context& ctx);

    // Configuration
    std::string m_url;
    std::string m_pendingHtml;
    std::string m_pendingBaseUrl;
    int m_width = 1280;
    int m_height = 720;
    bool m_transparent = false;
    bool m_javaScriptEnabled = true;
    float m_zoom = 1.0f;
    bool m_inputEnabled = true;
    int m_frameRate = 0;  // 0 = match Vivid
    int m_inputOffsetX = 0;  // Offset from window origin to WebView position
    int m_inputOffsetY = 0;

    // State
    bool m_needsReload = false;
    bool m_sizeChanged = false;
    std::string m_currentUrl;
    std::string m_pageTitle;

    // Platform backend
    std::unique_ptr<WebViewBackend> m_backend;

    // Fallback texture for when webview fails to load
    WGPUTexture m_fallbackTexture = nullptr;
    WGPUTextureView m_fallbackTextureView = nullptr;

    // Active texture (borrowed from backend or fallback)
    WGPUTexture m_activeTexture = nullptr;
    WGPUTextureView m_activeView = nullptr;

    // JavaScript callbacks
    std::unordered_map<std::string, std::function<void(const std::string&)>> m_jsCallbacks;

    // Tracking for raw input state (to calculate transitions)
    bool m_prevMouseButtons[3] = {false, false, false};
    bool m_prevKeyDown[512] = {};
    glm::vec2 m_prevMousePos = {0, 0};

    // Global focus tracking - only one WebView can have keyboard focus
    static WebView* s_focusedWebView;
};

} // namespace vivid::webview
