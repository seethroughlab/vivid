#include "browser_impl.h"
#include "texture_provider.h"

#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_client.h>
#include <include/cef_task.h>
#include <include/wrapper/cef_helpers.h>

#include <cstring>

namespace vivid::cef {

// -----------------------------------------------------------------------------
// VividRenderHandler
// -----------------------------------------------------------------------------

VividRenderHandler::VividRenderHandler(int width, int height)
    : m_width(width), m_height(height) {
}

void VividRenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
    rect.Set(0, 0, m_width, m_height);
}

void VividRenderHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                                  PaintElementType type,
                                  const RectList& dirtyRects,
                                  const void* buffer,
                                  int width, int height) {
    // CPU path - standard for macOS/Linux (OnAcceleratedPaint only works on Windows)
    if (type != PET_VIEW) return;

    // If we have a texture provider, use it directly for CPU upload
    if (m_textureProvider) {
        m_textureProvider->importFromCPU(buffer, width, height);
        m_hasNewFrame = true;
        return;
    }

    // Legacy path: store in CPU buffer for later processing
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    size_t bufferSize = width * height * 4;
    if (m_cpuBuffer.size() != bufferSize) {
        m_cpuBuffer.resize(bufferSize);
    }

    std::memcpy(m_cpuBuffer.data(), buffer, bufferSize);
    m_bufferWidth = width;
    m_bufferHeight = height;
    m_hasNewFrame = true;
}

void VividRenderHandler::OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
                                            PaintElementType type,
                                            const RectList& dirtyRects,
                                            void* shared_handle) {
    // GPU accelerated path - only available on Windows with D3D11 shared textures
    // On macOS/Linux, OnPaint is called instead (CEF limitation)
    if (type != PET_VIEW) return;

    if (m_textureProvider) {
        m_textureProvider->importFromCEF(shared_handle);
    }

    m_hasNewFrame = true;
}

void VividRenderHandler::setSize(int width, int height) {
    m_width = width;
    m_height = height;
}

void VividRenderHandler::setTextureProvider(TextureProvider* provider) {
    m_textureProvider = provider;
}

// -----------------------------------------------------------------------------
// VividCefClient
// -----------------------------------------------------------------------------

VividCefClient::VividCefClient(CefRefPtr<VividRenderHandler> renderHandler)
    : m_renderHandler(renderHandler) {
}

// CefLifeSpanHandler
void VividCefClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    m_browser = browser;

    // Load pending URL if one was set before browser was ready
    if (!m_pendingUrl.empty()) {
        auto frame = browser->GetMainFrame();
        if (frame) {
            frame->LoadURL(m_pendingUrl);
        }
        m_pendingUrl.clear();
    }
}

bool VividCefClient::DoClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    return false;  // Allow close
}

void VividCefClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    m_browser = nullptr;
}

// CefLoadHandler
void VividCefClient::OnLoadStart(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  TransitionType transition_type) {
    if (frame->IsMain()) {
        m_isLoading = true;
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_currentUrl = frame->GetURL().ToString();
    }
}

void VividCefClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                int httpStatusCode) {
    if (frame->IsMain()) {
        m_isLoading = false;
        std::string url;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_currentUrl = frame->GetURL().ToString();
            url = m_currentUrl;
        }

        if (m_loadEndCallback) {
            m_loadEndCallback(url, httpStatusCode);
        }
    }
}

void VividCefClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  ErrorCode errorCode,
                                  const CefString& errorText,
                                  const CefString& failedUrl) {
    if (frame->IsMain()) {
        m_isLoading = false;

        // Report via console callback if available
        if (m_consoleCallback) {
            std::string msg = "Load error: " + errorText.ToString() +
                             " (" + std::to_string(errorCode) + ") for " +
                             failedUrl.ToString();
            m_consoleCallback(ConsoleMessage::Level::Error, msg, "CEF", 0);
        }
    }
}

// CefDisplayHandler
void VividCefClient::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_pageTitle = title.ToString();
}

bool VividCefClient::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                       cef_log_severity_t level,
                                       const CefString& message,
                                       const CefString& source,
                                       int line) {
    if (m_consoleCallback) {
        ConsoleMessage::Level lvl;
        switch (level) {
            case LOGSEVERITY_DEBUG:   lvl = ConsoleMessage::Level::Debug; break;
            case LOGSEVERITY_INFO:    lvl = ConsoleMessage::Level::Info; break;
            case LOGSEVERITY_WARNING: lvl = ConsoleMessage::Level::Warning; break;
            case LOGSEVERITY_ERROR:
            case LOGSEVERITY_FATAL:   lvl = ConsoleMessage::Level::Error; break;
            default:                  lvl = ConsoleMessage::Level::Info; break;
        }
        m_consoleCallback(lvl, message.ToString(), source.ToString(), line);
    }
    return false;  // Allow default handling
}

bool VividCefClient::OnCursorChange(CefRefPtr<CefBrowser> browser,
                                     CefCursorHandle cursor,
                                     cef_cursor_type_t type,
                                     const CefCursorInfo& custom_cursor_info) {
    if (m_cursorChangeCallback) {
        CursorType ct;
        switch (type) {
            case CT_POINTER:       ct = CursorType::Default; break;
            case CT_HAND:          ct = CursorType::Pointer; break;
            case CT_IBEAM:         ct = CursorType::Text; break;
            case CT_WAIT:          ct = CursorType::Wait; break;
            case CT_PROGRESS:      ct = CursorType::Progress; break;
            case CT_CROSS:         ct = CursorType::CrossHair; break;
            case CT_HELP:          ct = CursorType::Help; break;
            case CT_MOVE:          ct = CursorType::Move; break;
            case CT_NORTHRESIZE:   ct = CursorType::ResizeN; break;
            case CT_SOUTHRESIZE:   ct = CursorType::ResizeS; break;
            case CT_EASTRESIZE:    ct = CursorType::ResizeE; break;
            case CT_WESTRESIZE:    ct = CursorType::ResizeW; break;
            case CT_NORTHEASTRESIZE: ct = CursorType::ResizeNE; break;
            case CT_NORTHWESTRESIZE: ct = CursorType::ResizeNW; break;
            case CT_SOUTHEASTRESIZE: ct = CursorType::ResizeSE; break;
            case CT_SOUTHWESTRESIZE: ct = CursorType::ResizeSW; break;
            case CT_EASTWESTRESIZE: ct = CursorType::ResizeEW; break;
            case CT_NORTHSOUTHRESIZE: ct = CursorType::ResizeNS; break;
            case CT_NORTHEASTSOUTHWESTRESIZE: ct = CursorType::ResizeNESW; break;
            case CT_NORTHWESTSOUTHEASTRESIZE: ct = CursorType::ResizeNWSE; break;
            case CT_NOTALLOWED:    ct = CursorType::NotAllowed; break;
            case CT_GRAB:          ct = CursorType::Grab; break;
            case CT_GRABBING:      ct = CursorType::Grabbing; break;
            case CT_CUSTOM:        ct = CursorType::Custom; break;
            default:               ct = CursorType::Default; break;
        }
        m_cursorChangeCallback(ct);
    }
    return false;  // Use default cursor handling
}

// CefRequestHandler
bool VividCefClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefFrame> frame,
                                     CefRefPtr<CefRequest> request,
                                     bool user_gesture,
                                     bool is_redirect) {
    return false;  // Allow navigation
}

// Handle IPC messages from renderer process (JS callbacks)
bool VividCefClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                               CefRefPtr<CefFrame> frame,
                                               CefProcessId source_process,
                                               CefRefPtr<CefProcessMessage> message) {
    if (message->GetName() == "vivid_callback") {
        CefRefPtr<CefListValue> args = message->GetArgumentList();
        if (args->GetSize() >= 2) {
            std::string callbackName = args->GetString(0).ToString();
            std::string data = args->GetString(1).ToString();

            // Look up and invoke the callback
            JSCallback callback;
            {
                std::lock_guard<std::mutex> lock(m_jsCallbackMutex);
                auto it = m_jsCallbacks.find(callbackName);
                if (it != m_jsCallbacks.end()) {
                    callback = it->second;
                }
            }

            if (callback) {
                callback(data);
            }
        }
        return true;  // Message handled
    }
    return false;  // Not handled
}

void VividCefClient::registerJSCallback(const std::string& name, JSCallback callback) {
    std::lock_guard<std::mutex> lock(m_jsCallbackMutex);
    m_jsCallbacks[name] = std::move(callback);
}

JSCallback VividCefClient::getJSCallback(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_jsCallbackMutex);
    auto it = m_jsCallbacks.find(name);
    return (it != m_jsCallbacks.end()) ? it->second : nullptr;
}

// -----------------------------------------------------------------------------
// BrowserImpl
// -----------------------------------------------------------------------------

BrowserImpl::BrowserImpl() = default;

BrowserImpl::~BrowserImpl() {
    destroy();
}

bool BrowserImpl::create(WGPUDevice device, int width, int height, bool transparent) {
    if (m_created) return true;

    m_width = width;
    m_height = height;
    m_transparent = transparent;

    // Create texture provider
    m_textureProvider = TextureProvider::create();
    if (!m_textureProvider->init(device, width, height)) {
        fprintf(stderr, "[BrowserImpl] Failed to create texture provider\n");
        return false;
    }

    // Create render handler
    m_renderHandler = new VividRenderHandler(width, height);
    m_renderHandler->setTextureProvider(m_textureProvider.get());

    // Create CEF client
    m_client = new VividCefClient(m_renderHandler);

    // Configure browser settings
    CefBrowserSettings browserSettings;
    browserSettings.windowless_frame_rate = 60;

    // Transparent background
    if (transparent) {
        browserSettings.background_color = CefColorSetARGB(0, 0, 0, 0);
    }

    // Window info for offscreen rendering
    CefWindowInfo windowInfo;

#if defined(_WIN32)
    windowInfo.SetAsWindowless(nullptr);
#elif defined(__APPLE__)
    windowInfo.SetAsWindowless(nullptr);
#else
    windowInfo.SetAsWindowless(0);
#endif

    // Enable shared texture rendering
    windowInfo.shared_texture_enabled = true;
    windowInfo.external_begin_frame_enabled = false;  // Use CEF's internal frame timing

    // Create browser (asynchronously)
    CefBrowserHost::CreateBrowser(
        windowInfo,
        m_client,
        "",  // No initial URL
        browserSettings,
        nullptr,  // Extra info
        nullptr   // Request context
    );

    m_created = true;
    return true;
}

void BrowserImpl::destroy() {
    if (!m_created) return;

    if (m_client && m_client->browser()) {
        m_client->browser()->GetHost()->CloseBrowser(true);
    }

    m_client = nullptr;
    m_renderHandler = nullptr;
    m_textureProvider.reset();
    m_created = false;
}

// Content loading
void BrowserImpl::loadUrl(const std::string& url) {
    if (!m_client) return;

    // If browser is ready, load immediately
    if (m_client->browser()) {
        auto frame = m_client->browser()->GetMainFrame();
        if (frame) {
            frame->LoadURL(url);
        }
    } else {
        // Browser not ready yet, queue for when it's created
        m_client->setPendingUrl(url);
    }
}

void BrowserImpl::loadHtml(const std::string& html, const std::string& baseUrl) {
    if (!m_client || !m_client->browser()) return;

    auto frame = m_client->browser()->GetMainFrame();
    if (frame) {
        frame->LoadURL("data:text/html;charset=utf-8," + html);
    }
}

void BrowserImpl::reload(bool ignoreCache) {
    if (!m_client || !m_client->browser()) return;

    if (ignoreCache) {
        m_client->browser()->ReloadIgnoreCache();
    } else {
        m_client->browser()->Reload();
    }
}

void BrowserImpl::goBack() {
    if (!m_client || !m_client->browser()) return;
    m_client->browser()->GoBack();
}

void BrowserImpl::goForward() {
    if (!m_client || !m_client->browser()) return;
    m_client->browser()->GoForward();
}

void BrowserImpl::stop() {
    if (!m_client || !m_client->browser()) return;
    m_client->browser()->StopLoad();
}

// Configuration
void BrowserImpl::setSize(int width, int height) {
    if (width == m_width && height == m_height) return;

    m_width = width;
    m_height = height;

    if (m_renderHandler) {
        m_renderHandler->setSize(width, height);
    }

    if (m_textureProvider) {
        m_textureProvider->resize(width, height);
    }

    if (m_client && m_client->browser()) {
        m_client->browser()->GetHost()->WasResized();
    }
}

void BrowserImpl::setZoom(float level) {
    if (!m_client || !m_client->browser()) return;
    m_client->browser()->GetHost()->SetZoomLevel(level);
}

void BrowserImpl::setFrameRate(int fps) {
    if (!m_client || !m_client->browser()) return;
    m_client->browser()->GetHost()->SetWindowlessFrameRate(fps);
}

// JavaScript
void BrowserImpl::executeJS(const std::string& script, JSResultCallback callback) {
    if (!m_client || !m_client->browser()) {
        if (callback) callback(false, "Browser not ready");
        return;
    }

    auto frame = m_client->browser()->GetMainFrame();
    if (!frame) {
        if (callback) callback(false, "No main frame");
        return;
    }

    // For now, just execute without result callback
    // Full V8 integration would require CefV8Context
    frame->ExecuteJavaScript(script, frame->GetURL(), 0);

    if (callback) {
        // TODO: Implement proper result handling via V8
        callback(true, "");
    }
}

void BrowserImpl::registerJSCallback(const std::string& name, JSCallback callback) {
    // Store callback in both BrowserImpl and the client
    // The client handles IPC messages from the renderer process
    {
        std::lock_guard<std::mutex> lock(m_jsCallbackMutex);
        m_jsCallbacks[name] = callback;
    }

    // Also register with the client so it can handle IPC messages
    if (m_client) {
        m_client->registerJSCallback(name, std::move(callback));
    }
}

// Input handling
void BrowserImpl::sendMouseMove(int x, int y) {
    if (!m_client || !m_client->browser()) return;

    CefMouseEvent event;
    event.x = x;
    event.y = y;
    event.modifiers = 0;

    m_client->browser()->GetHost()->SendMouseMoveEvent(event, false);
}

void BrowserImpl::sendMouseButton(MouseButton btn, bool pressed, int x, int y, int clicks) {
    if (!m_client || !m_client->browser()) return;

    CefMouseEvent event;
    event.x = x;
    event.y = y;
    event.modifiers = 0;

    CefBrowserHost::MouseButtonType cefBtn;
    switch (btn) {
        case MouseButton::Left:   cefBtn = MBT_LEFT; break;
        case MouseButton::Middle: cefBtn = MBT_MIDDLE; break;
        case MouseButton::Right:  cefBtn = MBT_RIGHT; break;
        default: cefBtn = MBT_LEFT;
    }

    m_client->browser()->GetHost()->SendMouseClickEvent(event, cefBtn, !pressed, clicks);
}

void BrowserImpl::sendMouseWheel(int x, int y, float deltaX, float deltaY) {
    if (!m_client || !m_client->browser()) return;

    CefMouseEvent event;
    event.x = x;
    event.y = y;
    event.modifiers = 0;

    // CEF expects wheel deltas in pixels
    m_client->browser()->GetHost()->SendMouseWheelEvent(
        event,
        static_cast<int>(deltaX * 120),
        static_cast<int>(deltaY * 120)
    );
}

void BrowserImpl::sendKeyEvent(int keyCode, bool pressed) {
    if (!m_client || !m_client->browser()) return;

    CefKeyEvent event;
    event.windows_key_code = keyCode;  // Assuming GLFW key codes map reasonably
    event.native_key_code = keyCode;
    event.is_system_key = false;
    event.modifiers = 0;

    if (pressed) {
        event.type = KEYEVENT_RAWKEYDOWN;
    } else {
        event.type = KEYEVENT_KEYUP;
    }

    m_client->browser()->GetHost()->SendKeyEvent(event);
}

void BrowserImpl::sendCharacter(uint32_t codepoint) {
    if (!m_client || !m_client->browser()) return;

    CefKeyEvent event;
    event.type = KEYEVENT_CHAR;
    event.windows_key_code = static_cast<int>(codepoint);
    event.character = static_cast<char16_t>(codepoint);
    event.unmodified_character = event.character;
    event.modifiers = 0;
    event.is_system_key = false;

    m_client->browser()->GetHost()->SendKeyEvent(event);
}

void BrowserImpl::setFocus(bool focused) {
    if (!m_client || !m_client->browser()) return;
    m_client->browser()->GetHost()->SetFocus(focused);
}

// Rendering
void BrowserImpl::pumpMessages() {
    // Messages are pumped by pumpCefMessageLoop() in cef_app.cpp
    // This method is called per-browser if needed
}

bool BrowserImpl::hasNewFrame() const {
    return m_renderHandler && m_renderHandler->hasNewFrame();
}

void BrowserImpl::clearNewFrameFlag() {
    if (m_renderHandler) {
        m_renderHandler->clearNewFrameFlag();
    }
}

WGPUTexture BrowserImpl::getTexture() const {
    return m_textureProvider ? m_textureProvider->getTexture() : nullptr;
}

WGPUTextureView BrowserImpl::getTextureView() const {
    return m_textureProvider ? m_textureProvider->getCurrentTextureView() : nullptr;
}

// State
bool BrowserImpl::isLoading() const {
    return m_client && m_client->isLoading();
}

std::string BrowserImpl::currentUrl() const {
    return m_client ? m_client->currentUrl() : "";
}

std::string BrowserImpl::pageTitle() const {
    return m_client ? m_client->pageTitle() : "";
}

// DevTools
void BrowserImpl::openDevTools() {
    if (!m_client || !m_client->browser()) return;

    // TODO: DevTools support requires a windowed browser on macOS
    // For now, just log a message
    fprintf(stderr, "[Browser] DevTools not yet supported in windowless mode\n");

    // Alternative: Use remote debugging
    // Run CEF with --remote-debugging-port=9222 and open chrome://inspect
}

void BrowserImpl::closeDevTools() {
    if (!m_client || !m_client->browser()) return;
    m_client->browser()->GetHost()->CloseDevTools();
}

// Callbacks
void BrowserImpl::setLoadEndCallback(LoadEndCallback cb) {
    if (m_client) {
        m_client->setLoadEndCallback(std::move(cb));
    }
}

void BrowserImpl::setConsoleCallback(ConsoleCallback cb) {
    if (m_client) {
        m_client->setConsoleCallback(std::move(cb));
    }
}

void BrowserImpl::setCursorChangeCallback(CursorChangeCallback cb) {
    if (m_client) {
        m_client->setCursorChangeCallback(std::move(cb));
    }
}

} // namespace vivid::cef
