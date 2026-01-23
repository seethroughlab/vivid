#pragma once

/**
 * @file browser.h
 * @brief CEF-based Browser operator for rendering web content with WebGL support
 *
 * The Browser class uses Chromium Embedded Framework for proper offscreen rendering,
 * supporting WebGL, three.js, and modern web APIs. It uses multi-process architecture
 * (like Chrome) for stability and rendering quality.
 *
 * Key features:
 * - Zero-copy GPU texture sharing where possible (IOSurface on macOS, D3D11 on Windows)
 * - Full WebGL/WebGL2 support for three.js and other 3D frameworks
 * - JavaScript interop with C++ callbacks
 * - Optional input handling via lambdas
 * - DevTools support for debugging
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/cef/export.h>
#include <vivid/cef/types.h>
#include <webgpu/webgpu.h>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>

namespace vivid::cef {

// Forward declarations
class BrowserImpl;

/**
 * @brief CEF-based browser operator for rendering web content to texture
 *
 * Renders HTML/CSS/JS content (including WebGL/three.js) to a GPU texture
 * for use in Vivid chains. Uses Chromium Embedded Framework for proper
 * web rendering with full WebGL support.
 *
 * @par Basic Usage
 * @code
 * auto& browser = chain.add<Browser>("threejs");
 * browser.setUrl("file://assets/webgl/scene.html");
 * browser.setSize(1920, 1080);
 * chain.output("threejs");
 * @endcode
 *
 * @par Interactive UI Overlay
 * @code
 * auto& ui = chain.add<Browser>("ui");
 * ui.setUrl("file://assets/ui/controls.html");
 * ui.setTransparent(true);
 * ui.setInputEnabled(true);
 * ui.registerCallback("onSliderChange", [&noise](const std::string& json) {
 *     // Update noise parameters from UI
 * });
 *
 * void update(Context& ctx) {
 *     ui.processInput(ctx);  // Forward input to browser
 *     ctx.chain().process(ctx);
 * }
 * @endcode
 */
class VIVID_CEF_API Browser : public effects::TextureOperator {
public:
    Browser();
    ~Browser() override;

    // Non-copyable
    Browser(const Browser&) = delete;
    Browser& operator=(const Browser&) = delete;

    // -------------------------------------------------------------------------
    /// @name Content Loading
    /// @{

    /**
     * @brief Set the URL to load
     * @param url URL (http://, https://, or file:// for local content)
     *
     * For local HTML files, use file:// URLs relative to project assets:
     * @code
     * browser.setUrl("file://assets/webgl/scene.html");
     * @endcode
     */
    void setUrl(const std::string& url);

    /**
     * @brief Load HTML content directly from string
     * @param html HTML content
     * @param baseUrl Optional base URL for relative resource paths
     */
    void loadHtml(const std::string& html, const std::string& baseUrl = "");

    /**
     * @brief Reload current page
     * @param ignoreCache If true, bypasses the cache
     */
    void reload(bool ignoreCache = false);

    /**
     * @brief Navigate back in history
     */
    void goBack();

    /**
     * @brief Navigate forward in history
     */
    void goForward();

    /**
     * @brief Stop loading current page
     */
    void stop();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set the browser render size in pixels
     * @param width Width in pixels
     * @param height Height in pixels
     */
    void setSize(int width, int height);

    /**
     * @brief Enable transparent background for UI overlays
     * @param transparent If true, browser background is transparent
     */
    void setTransparent(bool transparent);

    /**
     * @brief Set zoom level
     * @param level Zoom level (1.0 = 100%)
     */
    void setZoom(float level);

    /**
     * @brief Set target frame rate for browser rendering
     * @param fps Target frames per second (default: 60)
     */
    void setFrameRate(int fps);

    /// @}
    // -------------------------------------------------------------------------
    /// @name JavaScript Interop
    /// @{

    /**
     * @brief Execute JavaScript in the browser
     * @param script JavaScript code to execute
     * @param callback Optional callback with result (success flag and value as JSON)
     *
     * @code
     * browser.executeJS("document.title", [](bool ok, const std::string& val) {
     *     if (ok) printf("Title: %s\n", val.c_str());
     * });
     * @endcode
     */
    void executeJS(const std::string& script, JSResultCallback callback = nullptr);

    /**
     * @brief Register a callback that JavaScript can invoke
     * @param name Function name accessible from JS as window.vivid.name()
     * @param callback C++ function to call with JSON arguments
     *
     * @code
     * browser.registerCallback("onButtonClick", [](const std::string& args) {
     *     // args is JSON: {"buttonId": "start"}
     * });
     *
     * // In JavaScript:
     * // window.vivid.onButtonClick(JSON.stringify({buttonId: "start"}));
     * @endcode
     */
    void registerCallback(const std::string& name, JSCallback callback);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Input Handling
    /// @{

    /**
     * @brief Enable/disable automatic input forwarding
     * @param enabled If true, processInput() will forward events to browser
     */
    void setInputEnabled(bool enabled);

    /**
     * @brief Set input offset for positioned browsers
     * @param x X offset from window origin to browser top-left
     * @param y Y offset from window origin to browser top-left
     */
    void setInputOffset(int x, int y);

    /**
     * @brief Process input from Context (convenience method)
     * @param ctx Runtime context with input state
     *
     * Call this in your update() if you want the browser to receive input.
     * Only processes input if setInputEnabled(true) was called.
     */
    void processInput(Context& ctx);

    /**
     * @brief Process with raw input state (bypasses Context)
     * @param input Raw input state read directly from GLFW
     */
    void processRawInput(const RawInputState& input);

    // Manual input API (for custom handling)
    void sendMouseMove(int x, int y);
    void sendMouseButton(MouseButton btn, bool pressed, int x, int y, int clicks = 1);
    void sendMouseWheel(int x, int y, float deltaX, float deltaY);
    void sendKeyEvent(int keyCode, bool pressed, uint32_t modifiers = 0);
    void sendCharacter(uint32_t codepoint);

    /**
     * @brief Set callback to intercept key events before CEF processing
     * @param callback Return true to consume event, false to pass to CEF
     *
     * Use this for terminal input or custom keybinding handlers.
     * Example: Terminal intercepts Enter/arrows, passes Cmd+C to CEF for copy.
     */
    void setKeyInterceptCallback(KeyInterceptCallback callback);

    /**
     * @brief Set callback to intercept character input before CEF processing
     * @param callback Return true to consume, false to pass to CEF
     */
    void setCharInterceptCallback(CharInterceptCallback callback);

    /**
     * @brief Set terminal mode for keyboard intercept behavior
     * @param enabled If true, intercept callbacks are active; if false, input passes to CEF
     *
     * Use this to switch between terminal mode (intercepts go to PTY) and
     * editor mode (input goes to CEF/Monaco). JS can call window.vivid.setTerminalMode().
     */
    void setTerminalMode(bool enabled);

    /**
     * @brief Check if terminal mode is active
     */
    [[nodiscard]] bool isTerminalMode() const { return m_terminalMode; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Event Callbacks
    /// @{

    /**
     * @brief Set callback for page load completion
     * @param callback Called when page finishes loading
     */
    void onLoadEnd(LoadEndCallback callback);

    /**
     * @brief Set callback for JavaScript console messages
     * @param callback Called for each console.log/warn/error
     */
    void onConsole(ConsoleCallback callback);

    /**
     * @brief Set callback for cursor changes
     * @param callback Called when cursor type changes (for custom cursor rendering)
     */
    void onCursorChange(CursorChangeCallback callback);

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    /**
     * @brief Check if page is currently loading
     */
    [[nodiscard]] bool isLoading() const;

    /**
     * @brief Check if browser is ready to render
     */
    [[nodiscard]] bool isReady() const;

    /**
     * @brief Get current URL
     */
    [[nodiscard]] std::string currentUrl() const;

    /**
     * @brief Get current page title
     */
    [[nodiscard]] std::string pageTitle() const;

    /**
     * @brief Get browser width
     */
    [[nodiscard]] int browserWidth() const;

    /**
     * @brief Get browser height
     */
    [[nodiscard]] int browserHeight() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name DevTools
    /// @{

    /**
     * @brief Open Chrome DevTools for debugging
     *
     * Opens DevTools in a new window (requires CEF multi-process mode).
     */
    void openDevTools();

    /**
     * @brief Close DevTools window
     */
    void closeDevTools();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Focus Management
    /// @{

    /**
     * @brief Check if this browser currently has keyboard focus
     */
    [[nodiscard]] bool hasFocus() const { return s_focusedBrowser == this; }

    /**
     * @brief Request keyboard focus for this browser
     */
    void requestFocus();

    /**
     * @brief Release keyboard focus
     */
    void releaseFocus();

    /**
     * @brief Get the currently focused browser
     */
    static Browser* focusedBrowser() { return s_focusedBrowser; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name TextureOperator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Browser"; }

    WGPUTextureView outputView() const override;
    WGPUTexture outputTexture() const override;

    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}

private:
    void createFallbackTexture(Context& ctx);
    void updateFromPendingConfig();

    // Configuration (pending until init)
    std::string m_pendingUrl;
    std::string m_pendingHtml;
    std::string m_pendingBaseUrl;
    std::string m_projectDir;  // For resolving relative file:// URLs
    int m_configWidth = 1280;
    int m_configHeight = 720;
    bool m_transparent = false;
    float m_zoom = 1.0f;
    int m_frameRate = 60;
    bool m_inputEnabled = false;
    int m_inputOffsetX = 0;
    int m_inputOffsetY = 0;

    // State flags
    bool m_needsReload = false;
    bool m_sizeChanged = false;

    // Implementation (pimpl pattern for CEF isolation)
    std::unique_ptr<BrowserImpl> m_impl;

    // Fallback texture when CEF isn't available/ready
    WGPUTexture m_fallbackTexture = nullptr;
    WGPUTextureView m_fallbackView = nullptr;

    // Callbacks
    std::unordered_map<std::string, JSCallback> m_jsCallbacks;
    LoadEndCallback m_loadEndCallback;
    ConsoleCallback m_consoleCallback;
    CursorChangeCallback m_cursorChangeCallback;

    // Input tracking for raw input
    bool m_prevMouseButtons[3] = {false, false, false};
    bool m_prevKeyDown[512] = {};
    float m_prevMouseX = 0;
    float m_prevMouseY = 0;

    // Keyboard intercept callbacks
    KeyInterceptCallback m_keyInterceptCallback;
    CharInterceptCallback m_charInterceptCallback;
    bool m_terminalMode = false;  // Off by default; enable for terminal/PTY input

    // Global focus tracking
    static Browser* s_focusedBrowser;
};

// -------------------------------------------------------------------------
// Global CEF Lifecycle Functions
// -------------------------------------------------------------------------

/**
 * @brief Initialize CEF (call once at application startup)
 * @param argc Argument count from main()
 * @param argv Argument array from main()
 * @return true if initialization succeeded
 *
 * This starts the CEF browser process and must be called before creating
 * any Browser instances. On success, call shutdownCef() at exit.
 */
VIVID_CEF_API bool initializeCef(int argc, char* argv[]);

/**
 * @brief Shutdown CEF (call once at application exit)
 *
 * Cleans up all CEF resources. Call after all Browser instances are destroyed.
 */
VIVID_CEF_API void shutdownCef();

/**
 * @brief Pump the CEF message loop
 *
 * Call this once per frame from your main loop to process CEF events.
 * This is required for CEF to function properly.
 */
VIVID_CEF_API void pumpCefMessageLoop();

/**
 * @brief Check if CEF is initialized
 */
VIVID_CEF_API bool isCefInitialized();

} // namespace vivid::cef
