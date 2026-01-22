#pragma once

/**
 * @file browser_impl.h
 * @brief Internal implementation class for Browser (pimpl pattern)
 *
 * This header isolates CEF dependencies from the public API. The Browser class
 * holds a unique_ptr<BrowserImpl> and forwards calls to this implementation.
 */

#include <vivid/cef/types.h>
#include <webgpu/webgpu.h>

#include <include/cef_client.h>
#include <include/cef_browser.h>
#include <include/cef_render_handler.h>
#include <include/cef_life_span_handler.h>
#include <include/cef_load_handler.h>
#include <include/cef_display_handler.h>
#include <include/cef_request_handler.h>

#include <string>
#include <memory>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <condition_variable>

namespace vivid::cef {

// Forward declaration
class TextureProvider;

/**
 * @brief CEF render handler for offscreen rendering
 *
 * Receives paint callbacks from CEF and imports the rendered content
 * into GPU textures via TextureProvider.
 */
class VividRenderHandler : public CefRenderHandler {
public:
    explicit VividRenderHandler(int width, int height);

    // CefRenderHandler methods
    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;

    void OnPaint(CefRefPtr<CefBrowser> browser,
                 PaintElementType type,
                 const RectList& dirtyRects,
                 const void* buffer,
                 int width, int height) override;

    void OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
                           PaintElementType type,
                           const RectList& dirtyRects,
                           void* shared_handle) override;

    // Configuration
    void setSize(int width, int height);
    void setTextureProvider(TextureProvider* provider);

    // State
    bool hasNewFrame() const { return m_hasNewFrame.load(); }
    void clearNewFrameFlag() { m_hasNewFrame = false; }

    // CPU fallback buffer
    const void* cpuBuffer() const { return m_cpuBuffer.data(); }
    int bufferWidth() const { return m_bufferWidth; }
    int bufferHeight() const { return m_bufferHeight; }

private:
    int m_width;
    int m_height;
    std::atomic<bool> m_hasNewFrame{false};

    // CPU fallback buffer (for OnPaint)
    std::vector<uint8_t> m_cpuBuffer;
    int m_bufferWidth = 0;
    int m_bufferHeight = 0;
    std::mutex m_bufferMutex;

    // GPU texture provider (for OnAcceleratedPaint)
    TextureProvider* m_textureProvider = nullptr;

    IMPLEMENT_REFCOUNTING(VividRenderHandler);
};

/**
 * @brief CEF client that routes handler calls to Browser
 *
 * Implements CefClient and all handler interfaces, forwarding events
 * to the BrowserImpl for processing.
 */
class VividCefClient : public CefClient,
                       public CefLifeSpanHandler,
                       public CefLoadHandler,
                       public CefDisplayHandler,
                       public CefRequestHandler {
public:
    explicit VividCefClient(CefRefPtr<VividRenderHandler> renderHandler);

    // CefClient methods
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return m_renderHandler; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

    // CefLifeSpanHandler methods
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool DoClose(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    // CefLoadHandler methods
    void OnLoadStart(CefRefPtr<CefBrowser> browser,
                    CefRefPtr<CefFrame> frame,
                    TransitionType transition_type) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                  CefRefPtr<CefFrame> frame,
                  int httpStatusCode) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser,
                    CefRefPtr<CefFrame> frame,
                    ErrorCode errorCode,
                    const CefString& errorText,
                    const CefString& failedUrl) override;

    // CefDisplayHandler methods
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                         cef_log_severity_t level,
                         const CefString& message,
                         const CefString& source,
                         int line) override;
    bool OnCursorChange(CefRefPtr<CefBrowser> browser,
                       CefCursorHandle cursor,
                       cef_cursor_type_t type,
                       const CefCursorInfo& custom_cursor_info) override;

    // CefRequestHandler methods
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       CefRefPtr<CefRequest> request,
                       bool user_gesture,
                       bool is_redirect) override;

    // CefClient IPC method for receiving messages from renderer process
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;

    // Callback setters
    void setLoadEndCallback(LoadEndCallback cb) { m_loadEndCallback = std::move(cb); }
    void setConsoleCallback(ConsoleCallback cb) { m_consoleCallback = std::move(cb); }
    void setCursorChangeCallback(CursorChangeCallback cb) { m_cursorChangeCallback = std::move(cb); }

    // JS callback registration (called from renderer via IPC)
    void registerJSCallback(const std::string& name, JSCallback callback);
    JSCallback getJSCallback(const std::string& name) const;

    // State
    CefRefPtr<CefBrowser> browser() const { return m_browser; }
    bool isLoading() const { return m_isLoading; }
    const std::string& currentUrl() const { return m_currentUrl; }
    const std::string& pageTitle() const { return m_pageTitle; }

    // Set URL to load when browser is ready (for async creation)
    void setPendingUrl(const std::string& url) { m_pendingUrl = url; }

private:
    CefRefPtr<VividRenderHandler> m_renderHandler;
    CefRefPtr<CefBrowser> m_browser;

    std::atomic<bool> m_isLoading{false};
    std::string m_currentUrl;
    std::string m_pageTitle;
    std::mutex m_stateMutex;

    // Callbacks
    LoadEndCallback m_loadEndCallback;
    ConsoleCallback m_consoleCallback;
    CursorChangeCallback m_cursorChangeCallback;

    // Pending URL to load when browser is ready (async creation)
    std::string m_pendingUrl;

    // JS callbacks (called from renderer via IPC)
    std::unordered_map<std::string, JSCallback> m_jsCallbacks;
    mutable std::mutex m_jsCallbackMutex;

    IMPLEMENT_REFCOUNTING(VividCefClient);
};

/**
 * @brief Internal Browser implementation
 *
 * Holds CEF browser instance and all related state. This class is
 * created lazily when Browser::init() is called.
 */
class BrowserImpl {
public:
    BrowserImpl();
    ~BrowserImpl();

    // Initialization
    bool create(WGPUDevice device, int width, int height, bool transparent);
    void destroy();
    bool isCreated() const { return m_created; }

    // Content loading
    void loadUrl(const std::string& url);
    void loadHtml(const std::string& html, const std::string& baseUrl);
    void reload(bool ignoreCache);
    void goBack();
    void goForward();
    void stop();

    // Configuration
    void setSize(int width, int height);
    void setZoom(float level);
    void setFrameRate(int fps);

    // JavaScript
    void executeJS(const std::string& script, JSResultCallback callback);
    void registerJSCallback(const std::string& name, JSCallback callback);

    // Input
    void sendMouseMove(int x, int y);
    void sendMouseButton(MouseButton btn, bool pressed, int x, int y, int clicks);
    void sendMouseWheel(int x, int y, float deltaX, float deltaY);
    void sendKeyEvent(int keyCode, bool pressed);
    void sendCharacter(uint32_t codepoint);
    void setFocus(bool focused);

    // Rendering
    void pumpMessages();
    bool hasNewFrame() const;
    void clearNewFrameFlag();
    WGPUTexture getTexture() const;
    WGPUTextureView getTextureView() const;

    // State
    bool isLoading() const;
    bool isReady() const { return m_created && !isLoading(); }
    std::string currentUrl() const;
    std::string pageTitle() const;

    // DevTools
    void openDevTools();
    void closeDevTools();

    // Callbacks
    void setLoadEndCallback(LoadEndCallback cb);
    void setConsoleCallback(ConsoleCallback cb);
    void setCursorChangeCallback(CursorChangeCallback cb);

private:
    bool m_created = false;
    int m_width = 1280;
    int m_height = 720;
    bool m_transparent = false;

    CefRefPtr<VividRenderHandler> m_renderHandler;
    CefRefPtr<VividCefClient> m_client;
    std::unique_ptr<TextureProvider> m_textureProvider;

    // JS callbacks registered from C++
    std::unordered_map<std::string, JSCallback> m_jsCallbacks;
    std::mutex m_jsCallbackMutex;

    // Pending JS executions with callbacks
    struct PendingJS {
        std::string script;
        JSResultCallback callback;
    };
    std::vector<PendingJS> m_pendingJS;
    std::mutex m_pendingJSMutex;
};

} // namespace vivid::cef
