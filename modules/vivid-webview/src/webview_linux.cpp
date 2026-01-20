/**
 * @file webview_linux.cpp
 * @brief Linux WebView backend stub using CEF (Chromium Embedded Framework)
 *
 * TODO: Implement using CEF
 * Requires:
 * - CEF binary distribution (~200MB)
 * - CEF subprocess executable
 * - CEF message loop integration
 *
 * Implementation notes:
 * - Use CefBrowserHost::CreateBrowserSync() with windowless rendering
 * - Implement CefRenderHandler::OnPaint() for offscreen rendering
 * - Use GetViewRect() to set render size
 * - Forward input via SendMouseClickEvent(), SendKeyEvent(), etc.
 * - Integrate CefDoMessageLoopWork() with Vivid's main loop
 */

#include <vivid/webview/webview_backend.h>
#include <iostream>

namespace vivid::webview {

/**
 * @brief Linux WebView backend stub
 *
 * Currently not implemented. Returns nullptr from createWebViewBackend()
 * on Linux until CEF integration is complete.
 */
class WebViewLinux : public WebViewBackend {
public:
    WebViewLinux() = default;
    ~WebViewLinux() override = default;

    bool init(Context& ctx, int width, int height) override {
        std::cerr << "[WebView] Linux CEF backend not yet implemented" << std::endl;
        return false;
    }

    bool update(Context& ctx) override { return false; }
    void cleanup() override {}

    void loadUrl(const std::string& url) override {}
    void loadHtml(const std::string& html, const std::string& baseUrl) override {}
    void reload() override {}
    void stop() override {}
    void goBack() override {}
    void goForward() override {}

    void resize(int width, int height) override {}
    void setTransparent(bool transparent) override {}
    void setJavaScriptEnabled(bool enabled) override {}
    void setZoom(float zoom) override {}

    void executeJS(const std::string& script,
                  std::function<void(const std::string&)> callback) override {
        if (callback) callback("");
    }

    void registerCallback(const std::string& name,
                          std::function<void(const std::string&)> callback) override {}

    void sendMouseEvent(MouseEventType type, float x, float y,
                        MouseButton button, float scrollDeltaX, float scrollDeltaY,
                        KeyModifiers modifiers) override {}

    void sendKeyEvent(KeyEventType type, int keyCode, int scanCode,
                     uint32_t character, KeyModifiers modifiers) override {}

    void setFocus(bool focused) override {}

    [[nodiscard]] bool isLoading() const override { return false; }
    [[nodiscard]] bool isReady() const override { return false; }
    [[nodiscard]] std::string currentUrl() const override { return ""; }
    [[nodiscard]] std::string pageTitle() const override { return ""; }
    [[nodiscard]] bool canGoBack() const override { return false; }
    [[nodiscard]] bool canGoForward() const override { return false; }

    [[nodiscard]] WGPUTexture texture() const override { return nullptr; }
    [[nodiscard]] WGPUTextureView textureView() const override { return nullptr; }
    [[nodiscard]] int width() const override { return 0; }
    [[nodiscard]] int height() const override { return 0; }
};

std::unique_ptr<WebViewBackend> createWebViewBackend() {
    // Return nullptr to indicate backend not available
    // The WebView operator will create a fallback texture
    std::cerr << "[WebView] Linux backend not implemented - using fallback" << std::endl;
    return nullptr;
}

} // namespace vivid::webview
