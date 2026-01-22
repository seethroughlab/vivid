/**
 * @file webview.cpp
 * @brief WebView operator implementation
 *
 * Renders interactive web content (HTML/CSS/JS/WebGL) to texture.
 * Handles input routing from Vivid to the platform-specific backend.
 */

#include <vivid/webview/webview.h>
#include <vivid/webview/webview_backend.h>
#include <vivid/context.h>
#include <vivid/operator_registry.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cstring>

// Helper to create WGPUStringView from C string
inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

namespace vivid::webview {

// Register the operator
REGISTER_OPERATOR(WebView, "Effects", "Renders web content (HTML/CSS/JS) to texture", false);

// Static focus tracking
WebView* WebView::s_focusedWebView = nullptr;

WebView::WebView() = default;

WebView::~WebView() {
    cleanup();
}

void WebView::init(Context& ctx) {
    // Create platform-specific backend
    m_backend = createWebViewBackend();
    if (!m_backend) {
        std::cerr << "[WebView] Failed to create platform backend" << std::endl;
        createFallbackTexture(ctx);
        return;
    }

    // Initialize backend
    if (!m_backend->init(ctx, m_width, m_height)) {
        std::cerr << "[WebView] Backend initialization failed" << std::endl;
        m_backend.reset();
        createFallbackTexture(ctx);
        return;
    }

    // Apply initial configuration
    m_backend->setTransparent(m_transparent);
    m_backend->setZoom(m_zoom);

    // Register any pending JS callbacks
    for (const auto& [name, callback] : m_jsCallbacks) {
        m_backend->registerCallback(name, callback);
    }

    // Load initial content
    if (!m_pendingHtml.empty()) {
        m_backend->loadHtml(m_pendingHtml, m_pendingBaseUrl);
        m_pendingHtml.clear();
        m_pendingBaseUrl.clear();
    } else if (!m_url.empty()) {
        m_backend->loadUrl(m_url);
    }

    // Set active texture from backend
    m_activeTexture = m_backend->texture();
    m_activeView = m_backend->textureView();
}

void WebView::process(Context& ctx) {
    if (!m_backend) {
        return;
    }

    // Handle size changes
    if (m_sizeChanged) {
        m_backend->resize(m_width, m_height);
        m_sizeChanged = false;
    }

    // Handle URL reload
    if (m_needsReload) {
        if (!m_pendingHtml.empty()) {
            m_backend->loadHtml(m_pendingHtml, m_pendingBaseUrl);
            m_pendingHtml.clear();
            m_pendingBaseUrl.clear();
        } else if (!m_url.empty()) {
            m_backend->loadUrl(m_url);
        }
        m_needsReload = false;
    }

    // Handle input if enabled
    if (m_inputEnabled) {
        handleInputEvents(ctx);
    }

    // Update backend and capture frame
    bool gotNewFrame = m_backend->update(ctx);

    // Update active texture
    m_activeTexture = m_backend->texture();
    m_activeView = m_backend->textureView();

    // Update state
    m_currentUrl = m_backend->currentUrl();
    m_pageTitle = m_backend->pageTitle();

    if (gotNewFrame) {
        didCook();
    }
}

void WebView::processWithRawInput(Context& ctx, const RawInputState& input) {
    if (!m_backend) {
        return;
    }

    // Handle size changes
    if (m_sizeChanged) {
        m_backend->resize(m_width, m_height);
        m_sizeChanged = false;
    }

    // Handle URL reload
    if (m_needsReload) {
        if (!m_pendingHtml.empty()) {
            m_backend->loadHtml(m_pendingHtml, m_pendingBaseUrl);
            m_pendingHtml.clear();
            m_pendingBaseUrl.clear();
        } else if (!m_url.empty()) {
            m_backend->loadUrl(m_url);
        }
        m_needsReload = false;
    }

    // Handle input if enabled
    if (m_inputEnabled) {
        // Build modifiers
        KeyModifiers mods;
        mods.shift = input.shiftHeld;
        mods.ctrl = input.ctrlHeld;
        mods.alt = input.altHeld;
        mods.meta = input.superHeld;

        // Apply input offset for positioned WebViews
        float webX = input.mouseX - m_inputOffsetX;
        float webY = input.mouseY - m_inputOffsetY;

        // Check if mouse is within WebView bounds
        bool mouseInBounds = webX >= 0 && webY >= 0 &&
                             webX < m_width && webY < m_height;

        // Calculate mouse button transitions
        bool pressed[3], released[3], held[3];
        for (int i = 0; i < 3; ++i) {
            held[i] = input.mouseButtons[i];
            pressed[i] = input.mouseButtons[i] && !m_prevMouseButtons[i];
            released[i] = !input.mouseButtons[i] && m_prevMouseButtons[i];
        }

        // Track which button is held for drag operations
        MouseButton heldButton = MouseButton::Left;
        bool anyButtonHeld = false;
        for (int i = 0; i < 3; ++i) {
            if (held[i]) {
                heldButton = static_cast<MouseButton>(i);
                anyButtonHeld = true;
                break;
            }
        }

        if (mouseInBounds || anyButtonHeld) {
            // Mouse move - send when mouse moves
            glm::vec2 currentPos(input.mouseX, input.mouseY);
            if (currentPos != m_prevMousePos) {
                m_backend->sendMouseEvent(MouseEventType::Move, webX, webY,
                                          heldButton, 0, 0, mods);
            }

            // Mouse buttons
            for (int i = 0; i < 3; ++i) {
                MouseButton button = static_cast<MouseButton>(i);

                if (pressed[i]) {
                    // Request focus when clicked inside the webview
                    if (mouseInBounds) {
                        requestFocus();
                    }
                    m_backend->sendMouseEvent(MouseEventType::Down, webX, webY,
                                              button, 0, 0, mods);
                }
                if (released[i]) {
                    m_backend->sendMouseEvent(MouseEventType::Up, webX, webY,
                                              button, 0, 0, mods);
                }
            }

            // Scroll
            if (input.scrollX != 0 || input.scrollY != 0) {
                m_backend->sendMouseEvent(MouseEventType::Scroll, webX, webY,
                                          MouseButton::Left, input.scrollX, input.scrollY, mods);
            }
        }

        // Only forward keyboard events if this WebView has focus
        if (hasFocus()) {
            // Key events - calculate transitions from raw state
            for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
                bool keyHeld = input.keyDown[key];
                bool keyPressed = keyHeld && !m_prevKeyDown[key];
                bool keyReleased = !keyHeld && m_prevKeyDown[key];

                if (keyPressed) {
                    m_backend->sendKeyEvent(KeyEventType::Down, key, key, 0, mods);
                }
                if (keyReleased) {
                    m_backend->sendKeyEvent(KeyEventType::Up, key, key, 0, mods);
                }
            }

            // Character input
            for (uint32_t codepoint : input.characterInput) {
                m_backend->sendKeyEvent(KeyEventType::Char, 0, 0, codepoint, mods);
            }
        }

        // Update previous state for next frame
        for (int i = 0; i < 3; ++i) {
            m_prevMouseButtons[i] = input.mouseButtons[i];
        }
        m_prevMousePos = glm::vec2(input.mouseX, input.mouseY);
        std::memcpy(m_prevKeyDown, input.keyDown, sizeof(m_prevKeyDown));
    }

    // Update backend and capture frame
    bool gotNewFrame = m_backend->update(ctx);

    // Update active texture
    m_activeTexture = m_backend->texture();
    m_activeView = m_backend->textureView();

    // Update state
    m_currentUrl = m_backend->currentUrl();
    m_pageTitle = m_backend->pageTitle();

    if (gotNewFrame) {
        didCook();
    }
}

void WebView::cleanup() {
    // Release focus if we had it
    releaseFocus();

    if (m_backend) {
        m_backend->cleanup();
        m_backend.reset();
    }

    if (m_fallbackTextureView) {
        wgpuTextureViewRelease(m_fallbackTextureView);
        m_fallbackTextureView = nullptr;
    }
    if (m_fallbackTexture) {
        wgpuTextureRelease(m_fallbackTexture);
        m_fallbackTexture = nullptr;
    }

    m_activeTexture = nullptr;
    m_activeView = nullptr;
}

void WebView::requestFocus() {
    if (s_focusedWebView == this) return;

    // Unfocus previous WebView
    if (s_focusedWebView && s_focusedWebView->m_backend) {
        s_focusedWebView->m_backend->setFocus(false);
    }

    // Focus this WebView
    s_focusedWebView = this;
    if (m_backend) {
        m_backend->setFocus(true);
    }
}

void WebView::releaseFocus() {
    if (s_focusedWebView == this) {
        if (m_backend) {
            m_backend->setFocus(false);
        }
        s_focusedWebView = nullptr;
    }
}

void WebView::handleInputEvents(Context& ctx) {
    if (!m_backend) return;

    // Get current modifiers
    KeyModifiers mods;
    mods.shift = ctx.shiftHeld();
    mods.ctrl = ctx.ctrlHeld();
    mods.alt = ctx.altHeld();
    mods.meta = ctx.superHeld();

    // Get mouse position in window logical coordinates
    // Backend will scale to CSS pixels based on backing scale factor
    glm::vec2 mousePos = ctx.mouse();
    // Apply input offset (for positioned WebViews like IDE panel)
    float webX = mousePos.x - m_inputOffsetX;
    float webY = mousePos.y - m_inputOffsetY;

    // Check if mouse is within WebView bounds
    bool mouseInBounds = webX >= 0 && webY >= 0 &&
                         webX < m_width && webY < m_height;

    // Debug: log click events
    if (ctx.mouseButton(0).pressed) {
        std::cout << "[WebView] Click at window(" << mousePos.x << "," << mousePos.y
                  << ") -> webview(" << webX << "," << webY << ") inBounds=" << mouseInBounds
                  << " offset=(" << m_inputOffsetX << "," << m_inputOffsetY << ")" << std::endl;
    }

    // Track which mouse button is currently held for drag operations
    MouseButton heldButton = MouseButton::Left;
    bool anyButtonHeld = false;
    for (int i = 0; i < 3; ++i) {
        if (ctx.mouseButton(i).held) {
            heldButton = static_cast<MouseButton>(i);
            anyButtonHeld = true;
            break;
        }
    }

    if (mouseInBounds || anyButtonHeld) {
        // Mouse move - send whenever mouse moves, especially during drag
        glm::vec2 delta = ctx.mouseDelta();
        if (delta.x != 0 || delta.y != 0) {
            m_backend->sendMouseEvent(MouseEventType::Move, webX, webY,
                                      heldButton, 0, 0, mods);
        }

        // Mouse buttons
        for (int i = 0; i < 3; ++i) {
            const auto& btn = ctx.mouseButton(i);
            MouseButton button = static_cast<MouseButton>(i);

            if (btn.pressed) {
                // Request focus when clicked inside the webview
                if (mouseInBounds) {
                    requestFocus();
                }
                m_backend->sendMouseEvent(MouseEventType::Down, webX, webY,
                                          button, 0, 0, mods);
            }
            if (btn.released) {
                m_backend->sendMouseEvent(MouseEventType::Up, webX, webY,
                                          button, 0, 0, mods);
            }
        }

        // Scroll
        glm::vec2 scroll = ctx.scroll();
        if (scroll.x != 0 || scroll.y != 0) {
            m_backend->sendMouseEvent(MouseEventType::Scroll, webX, webY,
                                      MouseButton::Left, scroll.x, scroll.y, mods);
        }
    }

    // Only forward keyboard/character events if this WebView has focus
    if (!hasFocus()) return;

    // Key events
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
        const auto& keyState = ctx.key(key);

        if (keyState.pressed) {
            m_backend->sendKeyEvent(KeyEventType::Down, key, key, 0, mods);
        }
        if (keyState.released) {
            m_backend->sendKeyEvent(KeyEventType::Up, key, key, 0, mods);
        }
    }

    // Character input - forward typed characters for text editors/terminals
    for (uint32_t codepoint : ctx.characterInput()) {
        m_backend->sendKeyEvent(KeyEventType::Char, 0, 0, codepoint, mods);
    }
}

void WebView::createFallbackTexture(Context& ctx) {
    const uint32_t texWidth = m_width > 0 ? m_width : 320;
    const uint32_t texHeight = m_height > 0 ? m_height : 180;

    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("WebView Fallback");
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {texWidth, texHeight, 1};
    texDesc.format = WGPUTextureFormat_BGRA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    m_fallbackTexture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = toStringView("WebView Fallback View");
    viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;

    m_fallbackTextureView = wgpuTextureCreateView(m_fallbackTexture, &viewDesc);

    // Fill with dark gray
    std::vector<uint8_t> pixels(texWidth * texHeight * 4, 0);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 40;      // B
        pixels[i + 1] = 40;  // G
        pixels[i + 2] = 40;  // R
        pixels[i + 3] = 255; // A
    }

    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = m_fallbackTexture;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = texWidth * 4;
    dataLayout.rowsPerImage = texHeight;

    WGPUExtent3D writeSize = {texWidth, texHeight, 1};

    wgpuQueueWriteTexture(ctx.queue(), &destination, pixels.data(),
                          pixels.size(), &dataLayout, &writeSize);

    m_activeTexture = m_fallbackTexture;
    m_activeView = m_fallbackTextureView;
}

// Configuration methods

void WebView::setUrl(const std::string& url) {
    m_url = url;
    m_needsReload = true;
    markDirty();
}

void WebView::loadHtml(const std::string& html, const std::string& baseUrl) {
    m_pendingHtml = html;
    m_pendingBaseUrl = baseUrl;
    m_needsReload = true;
    markDirty();
}

void WebView::setSize(int width, int height) {
    if (m_width != width || m_height != height) {
        m_width = width;
        m_height = height;
        m_sizeChanged = true;
        markDirty();
    }
}

void WebView::setTransparent(bool transparent) {
    if (m_transparent != transparent) {
        m_transparent = transparent;
        if (m_backend) {
            m_backend->setTransparent(transparent);
        }
        markDirty();
    }
}

void WebView::setJavaScriptEnabled(bool enabled) {
    m_javaScriptEnabled = enabled;
    if (m_backend) {
        m_backend->setJavaScriptEnabled(enabled);
    }
}

void WebView::setZoom(float zoom) {
    m_zoom = zoom;
    if (m_backend) {
        m_backend->setZoom(zoom);
    }
    markDirty();
}

void WebView::setInputEnabled(bool enabled) {
    m_inputEnabled = enabled;
}

void WebView::setInputOffset(int x, int y) {
    m_inputOffsetX = x;
    m_inputOffsetY = y;
}

void WebView::setFrameRate(int fps) {
    m_frameRate = fps;
}

// JavaScript interop

void WebView::executeJS(const std::string& script,
                        std::function<void(const std::string&)> callback) {
    if (m_backend) {
        m_backend->executeJS(script, callback);
    } else if (callback) {
        callback("");
    }
}

void WebView::registerCallback(const std::string& name,
                               std::function<void(const std::string&)> callback) {
    m_jsCallbacks[name] = callback;
    if (m_backend) {
        m_backend->registerCallback(name, callback);
    }
}

// State queries

bool WebView::isLoading() const {
    return m_backend ? m_backend->isLoading() : false;
}

bool WebView::isReady() const {
    return m_backend ? m_backend->isReady() : false;
}

const std::string& WebView::currentUrl() const {
    return m_currentUrl;
}

const std::string& WebView::pageTitle() const {
    return m_pageTitle;
}

int WebView::webviewWidth() const {
    return m_width;
}

int WebView::webviewHeight() const {
    return m_height;
}

// Navigation

void WebView::reload() {
    if (m_backend) {
        m_backend->reload();
    }
}

void WebView::goBack() {
    if (m_backend) {
        m_backend->goBack();
    }
}

void WebView::goForward() {
    if (m_backend) {
        m_backend->goForward();
    }
}

void WebView::stop() {
    if (m_backend) {
        m_backend->stop();
    }
}

} // namespace vivid::webview
