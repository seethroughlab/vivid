/**
 * @file webview_windows.cpp
 * @brief Windows WebView backend stub using WebView2
 *
 * TODO: Implement using Microsoft WebView2 SDK
 * Requires:
 * - Microsoft.WebView2 NuGet package
 * - Edge WebView2 Runtime installed on user's machine
 *
 * Implementation notes:
 * - Use ICoreWebView2 interface for webview control
 * - Use composition mode with CreateCoreWebView2CompositionController
 * - Get rendered content via CapturePreview() or shared DXGI textures
 * - Forward input via SendMouseInput() and SendKeyboardInput()
 */

#include <vivid/webview/webview_backend.h>
#include <iostream>

namespace vivid::webview {

/**
 * @brief Windows WebView backend stub
 *
 * Currently not implemented. Returns nullptr from createWebViewBackend()
 * on Windows until WebView2 integration is complete.
 */
class WebViewWindows : public WebViewBackend {
public:
    WebViewWindows() = default;
    ~WebViewWindows() override = default;

    bool init(Context& ctx, int width, int height) override {
        std::cerr << "[WebView] Windows WebView2 backend not yet implemented" << std::endl;
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
    std::cerr << "[WebView] Windows backend not implemented - using fallback" << std::endl;
    return nullptr;
}

} // namespace vivid::webview
