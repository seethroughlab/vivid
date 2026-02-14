#include "browser_impl.h"
#include "cef_app.h"
#include <vivid/cef/browser.h>
#include <vivid/context.h>

#include <filesystem>

namespace vivid::cef {

namespace {
// Resolve relative file:// URLs to absolute paths
std::string resolveFileUrl(const std::string &url,
                           const std::string &projectDir) {
  // Check if it's a file:// URL with a relative path
  if (url.substr(0, 7) == "file://") {
    std::string path = url.substr(7);
    // If path doesn't start with /, it's relative
    if (!path.empty() && path[0] != '/') {
      // Resolve relative to project directory
      std::filesystem::path absPath = std::filesystem::path(projectDir) / path;
      return "file://" + absPath.string();
    }
  }
  return url;
}

// Get project directory from chain path
std::string getProjectDir(const std::string &chainPath) {
  if (!chainPath.empty()) {
    std::filesystem::path p(chainPath);
    // Make absolute if relative
    if (p.is_relative()) {
      p = std::filesystem::current_path() / p;
    }
    if (p.has_parent_path()) {
      return p.parent_path().string();
    }
  }
  return std::filesystem::current_path().string();
}
} // namespace

// Static member
Browser *Browser::s_focusedBrowser = nullptr;

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------

Browser::Browser() = default;

Browser::~Browser() {
  if (s_focusedBrowser == this) {
    s_focusedBrowser = nullptr;
  }
  cleanup();
}

// -----------------------------------------------------------------------------
// Content Loading
// -----------------------------------------------------------------------------

void Browser::setUrl(const std::string &url) {
  // Store the original URL - will be resolved in init() when we have context
  m_pendingUrl = url;
  m_pendingHtml.clear();
  if (m_impl && m_impl->isCreated()) {
    // If already initialized, resolve using stored project dir
    std::string resolvedUrl = resolveFileUrl(url, m_projectDir);
    m_impl->loadUrl(resolvedUrl);
  }
}

void Browser::loadHtml(const std::string &html, const std::string &baseUrl) {
  m_pendingHtml = html;
  m_pendingBaseUrl = baseUrl;
  m_pendingUrl.clear();
  if (m_impl && m_impl->isCreated()) {
    m_impl->loadHtml(html, baseUrl);
  }
}

void Browser::reload(bool ignoreCache) {
  if (m_impl && m_impl->isCreated()) {
    m_impl->reload(ignoreCache);
  }
}

void Browser::goBack() {
  if (m_impl && m_impl->isCreated()) {
    m_impl->goBack();
  }
}

void Browser::goForward() {
  if (m_impl && m_impl->isCreated()) {
    m_impl->goForward();
  }
}

void Browser::stop() {
  if (m_impl && m_impl->isCreated()) {
    m_impl->stop();
  }
}

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

void Browser::setSize(int width, int height) {
  m_configWidth = width;
  m_configHeight = height;
  setResolution(width, height);
  if (m_impl && m_impl->isCreated()) {
    m_impl->setSize(width, height);
    m_sizeChanged = true;
  }
}

void Browser::setTransparent(bool transparent) { m_transparent = transparent; }

void Browser::setZoom(float level) {
  m_zoom = level;
  if (m_impl && m_impl->isCreated()) {
    m_impl->setZoom(level);
  }
}

void Browser::setFrameRate(int fps) {
  m_frameRate = fps;
  if (m_impl && m_impl->isCreated()) {
    m_impl->setFrameRate(fps);
  }
}

// -----------------------------------------------------------------------------
// JavaScript Interop
// -----------------------------------------------------------------------------

void Browser::executeJS(const std::string &script, JSResultCallback callback) {
  if (m_impl && m_impl->isCreated()) {
    m_impl->executeJS(script, std::move(callback));
  }
}

void Browser::registerCallback(const std::string &name, JSCallback callback) {
  m_jsCallbacks[name] = std::move(callback);
  if (m_impl && m_impl->isCreated()) {
    m_impl->registerJSCallback(name, m_jsCallbacks[name]);
  }
}

// -----------------------------------------------------------------------------
// Input Handling
// -----------------------------------------------------------------------------

void Browser::setInputEnabled(bool enabled) { m_inputEnabled = enabled; }

void Browser::setInputOffset(int x, int y) {
  m_inputOffsetX = x;
  m_inputOffsetY = y;
}

void Browser::processInput(Context &ctx) {
  if (!m_inputEnabled || !m_impl || !m_impl->isCreated())
    return;

  // Get mouse position from context
  glm::vec2 mousePos = ctx.mouse();

  // Convert to browser coordinates
  int bx = static_cast<int>(mousePos.x) - m_inputOffsetX;
  int by = static_cast<int>(mousePos.y) - m_inputOffsetY;

  // Mouse move
  if (bx != static_cast<int>(m_prevMouseX) ||
      by != static_cast<int>(m_prevMouseY)) {
    m_impl->sendMouseMove(bx, by);
    m_prevMouseX = static_cast<float>(bx);
    m_prevMouseY = static_cast<float>(by);
  }

  // Mouse buttons (0=left, 1=right, 2=middle)
  bool leftHeld = ctx.mouseButton(0).held;
  bool rightHeld = ctx.mouseButton(1).held;
  bool middleHeld = ctx.mouseButton(2).held;

  if (leftHeld != m_prevMouseButtons[0]) {
    m_impl->sendMouseButton(MouseButton::Left, leftHeld, bx, by, 1);
    m_prevMouseButtons[0] = leftHeld;
  }
  if (rightHeld != m_prevMouseButtons[1]) {
    m_impl->sendMouseButton(MouseButton::Right, rightHeld, bx, by, 1);
    m_prevMouseButtons[1] = rightHeld;
  }
  if (middleHeld != m_prevMouseButtons[2]) {
    m_impl->sendMouseButton(MouseButton::Middle, middleHeld, bx, by, 1);
    m_prevMouseButtons[2] = middleHeld;
  }

  // Mouse wheel
  glm::vec2 scroll = ctx.scroll();
  if (scroll.x != 0 || scroll.y != 0) {
    m_impl->sendMouseWheel(bx, by, scroll.x, scroll.y);
  }

  // Keyboard (only if focused)
  if (hasFocus()) {
    // Build modifier flags
    uint32_t mods = 0;
    if (ctx.shiftHeld())
      mods |= ModShift;
    if (ctx.ctrlHeld())
      mods |= ModControl;
    if (ctx.altHeld())
      mods |= ModAlt;
    if (ctx.superHeld())
      mods |= ModSuper;

    for (int key = 0; key < 512; ++key) {
      bool held = ctx.key(key).held;
      if (held != m_prevKeyDown[key]) {
        m_impl->sendKeyEvent(key, held, mods);
        m_prevKeyDown[key] = held;
      }
    }

    // Character input
    for (uint32_t ch : ctx.characterInput()) {
      m_impl->sendCharacter(ch);
    }
  }
}

void Browser::processRawInput(const RawInputState &input) {
  if (!m_inputEnabled || !m_impl || !m_impl->isCreated())
    return;

  int bx = static_cast<int>(input.mouseX) - m_inputOffsetX;
  int by = static_cast<int>(input.mouseY) - m_inputOffsetY;

  // Mouse move
  if (bx != static_cast<int>(m_prevMouseX) ||
      by != static_cast<int>(m_prevMouseY)) {
    m_impl->sendMouseMove(bx, by);
    m_prevMouseX = static_cast<float>(bx);
    m_prevMouseY = static_cast<float>(by);
  }

  // Mouse buttons
  for (int i = 0; i < 3; ++i) {
    if (input.mouseButtons[i] != m_prevMouseButtons[i]) {
      m_impl->sendMouseButton(static_cast<MouseButton>(i),
                              input.mouseButtons[i], bx, by, 1);
      m_prevMouseButtons[i] = input.mouseButtons[i];
    }
  }

  // Scroll
  if (input.scrollX != 0 || input.scrollY != 0) {
    m_impl->sendMouseWheel(bx, by, input.scrollX, input.scrollY);
  }

  // Keyboard - with intercept callbacks
  if (hasFocus()) {
    // Build modifier flags
    uint32_t mods = 0;
    if (input.shiftHeld)
      mods |= ModShift;
    if (input.ctrlHeld)
      mods |= ModControl;
    if (input.altHeld)
      mods |= ModAlt;
    if (input.superHeld)
      mods |= ModSuper;

    // Key events
    for (int key = 0; key < 512; ++key) {
      if (input.keyDown[key] != m_prevKeyDown[key]) {
        bool intercepted = false;
        if (m_keyInterceptCallback) {
          intercepted = m_keyInterceptCallback(key, input.keyDown[key], mods);
        }
        if (!intercepted) {
          m_impl->sendKeyEvent(key, input.keyDown[key], mods);
        }
        m_prevKeyDown[key] = input.keyDown[key];
      }
    }

    // Character events
    for (uint32_t ch : input.characterInput) {
      bool intercepted = false;
      if (m_charInterceptCallback) {
        intercepted = m_charInterceptCallback(ch);
      }
      if (!intercepted) {
        m_impl->sendCharacter(ch);
      }
    }
  }
}

void Browser::setKeyInterceptCallback(KeyInterceptCallback callback) {
  m_keyInterceptCallback = std::move(callback);
}

void Browser::setCharInterceptCallback(CharInterceptCallback callback) {
  m_charInterceptCallback = std::move(callback);
}

void Browser::setTerminalMode(bool enabled) { m_terminalMode = enabled; }

void Browser::sendMouseMove(int x, int y) {
  if (m_impl && m_impl->isCreated()) {
    m_impl->sendMouseMove(x - m_inputOffsetX, y - m_inputOffsetY);
  }
}

void Browser::sendMouseButton(MouseButton btn, bool pressed, int x, int y,
                              int clicks) {
  if (m_impl && m_impl->isCreated()) {
    m_impl->sendMouseButton(btn, pressed, x - m_inputOffsetX,
                            y - m_inputOffsetY, clicks);
  }
}

void Browser::sendMouseWheel(int x, int y, float deltaX, float deltaY) {
  if (m_impl && m_impl->isCreated()) {
    m_impl->sendMouseWheel(x - m_inputOffsetX, y - m_inputOffsetY, deltaX,
                           deltaY);
  }
}

void Browser::sendKeyEvent(int keyCode, bool pressed, uint32_t modifiers) {
  if (m_impl && m_impl->isCreated()) {
    m_impl->sendKeyEvent(keyCode, pressed, modifiers);
  }
}

void Browser::sendCharacter(uint32_t codepoint) {
  if (m_impl && m_impl->isCreated()) {
    m_impl->sendCharacter(codepoint);
  }
}

// -----------------------------------------------------------------------------
// Event Callbacks
// -----------------------------------------------------------------------------

void Browser::onLoadEnd(LoadEndCallback callback) {
  m_loadEndCallback = std::move(callback);
  if (m_impl) {
    m_impl->setLoadEndCallback(m_loadEndCallback);
  }
}

void Browser::onConsole(ConsoleCallback callback) {
  m_consoleCallback = std::move(callback);
  if (m_impl) {
    m_impl->setConsoleCallback(m_consoleCallback);
  }
}

void Browser::onCursorChange(CursorChangeCallback callback) {
  m_cursorChangeCallback = std::move(callback);
  if (m_impl) {
    m_impl->setCursorChangeCallback(m_cursorChangeCallback);
  }
}

// -----------------------------------------------------------------------------
// State Queries
// -----------------------------------------------------------------------------

bool Browser::isLoading() const { return m_impl && m_impl->isLoading(); }

bool Browser::isReady() const { return m_impl && m_impl->isReady(); }

std::string Browser::currentUrl() const {
  return m_impl ? m_impl->currentUrl() : "";
}

std::string Browser::pageTitle() const {
  return m_impl ? m_impl->pageTitle() : "";
}

int Browser::browserWidth() const { return m_configWidth; }

int Browser::browserHeight() const { return m_configHeight; }

// -----------------------------------------------------------------------------
// DevTools
// -----------------------------------------------------------------------------

void Browser::openDevTools() {
  if (m_impl && m_impl->isCreated()) {
    m_impl->openDevTools();
  }
}

void Browser::closeDevTools() {
  if (m_impl && m_impl->isCreated()) {
    m_impl->closeDevTools();
  }
}

// -----------------------------------------------------------------------------
// Focus Management
// -----------------------------------------------------------------------------

void Browser::requestFocus() {
  s_focusedBrowser = this;
  if (m_impl) {
    m_impl->setFocus(true);
  }
}

void Browser::releaseFocus() {
  if (s_focusedBrowser == this) {
    s_focusedBrowser = nullptr;
  }
  if (m_impl) {
    m_impl->setFocus(false);
  }
}

// -----------------------------------------------------------------------------
// TextureOperator Interface
// -----------------------------------------------------------------------------

void Browser::init(Context &ctx) {
  if (!beginInit())
    return;

  // Get project directory from chain path for resolving relative URLs
  m_projectDir = getProjectDir(ctx.chainPath());

  // Check if CEF is initialized
  if (!isCefInitialized()) {
    fprintf(stderr,
            "[Browser] CEF not initialized. Call initializeCef() first.\n");
    createFallbackTexture(ctx);
    return;
  }

  // Create implementation
  m_impl = std::make_unique<BrowserImpl>();
  if (!m_impl->create(ctx.device(), m_configWidth, m_configHeight,
                      m_transparent)) {
    fprintf(stderr, "[Browser] Failed to create CEF browser\n");
    m_impl.reset();
    createFallbackTexture(ctx);
    return;
  }

  // Set callbacks
  if (m_loadEndCallback)
    m_impl->setLoadEndCallback(m_loadEndCallback);
  if (m_consoleCallback)
    m_impl->setConsoleCallback(m_consoleCallback);
  if (m_cursorChangeCallback)
    m_impl->setCursorChangeCallback(m_cursorChangeCallback);

  // Register JS callbacks
  for (auto &[name, cb] : m_jsCallbacks) {
    m_impl->registerJSCallback(name, cb);
  }

  // Apply configuration
  m_impl->setZoom(m_zoom);
  m_impl->setFrameRate(m_frameRate);

  // Load content (resolve relative file:// URLs using project directory)
  if (!m_pendingUrl.empty()) {
    std::string resolvedUrl = resolveFileUrl(m_pendingUrl, m_projectDir);
    m_impl->loadUrl(resolvedUrl);
  } else if (!m_pendingHtml.empty()) {
    m_impl->loadHtml(m_pendingHtml, m_pendingBaseUrl);
  }
}

void Browser::process(Context &ctx) {
  markDirty(); // Always update

  if (!m_impl || !m_impl->isCreated()) {
    // Use fallback texture
    return;
  }

  // Pump CEF messages
  m_impl->pumpMessages();

  // Check for new frame
  if (m_impl->hasNewFrame()) {
    m_impl->clearNewFrameFlag();
  }

  didCook();
}

void Browser::cleanup() {
  if (m_impl) {
    m_impl->destroy();
    m_impl.reset();
  }

  if (m_fallbackView) {
    wgpuTextureViewRelease(m_fallbackView);
    m_fallbackView = nullptr;
  }
  if (m_fallbackTexture) {
    wgpuTextureRelease(m_fallbackTexture);
    m_fallbackTexture = nullptr;
  }
}

WGPUTextureView Browser::outputView() const {
  if (m_impl && m_impl->isCreated()) {
    WGPUTextureView view = m_impl->getTextureView();
    if (view)
      return view;
  }
  return m_fallbackView;
}

WGPUTexture Browser::outputTexture() const {
  if (m_impl && m_impl->isCreated()) {
    WGPUTexture tex = m_impl->getTexture();
    if (tex)
      return tex;
  }
  return m_fallbackTexture;
}

std::vector<ParamDecl> Browser::params() {
  return {{"zoom", ParamType::Float, 0.25f, 4.0f, {m_zoom, 0, 0, 0}},
          {"transparent",
           ParamType::Float,
           0.0f,
           1.0f,
           {m_transparent ? 1.0f : 0.0f, 0, 0, 0}},
          {"inputEnabled",
           ParamType::Float,
           0.0f,
           1.0f,
           {m_inputEnabled ? 1.0f : 0.0f, 0, 0, 0}},
          {"frameRate",
           ParamType::Int,
           1,
           120,
           {static_cast<float>(m_frameRate), 0, 0, 0}}};
}

bool Browser::getParam(const std::string &name, float out[4]) {
  if (name == "zoom") {
    out[0] = m_zoom;
    return true;
  }
  if (name == "transparent") {
    out[0] = m_transparent ? 1.0f : 0.0f;
    return true;
  }
  if (name == "inputEnabled") {
    out[0] = m_inputEnabled ? 1.0f : 0.0f;
    return true;
  }
  if (name == "frameRate") {
    out[0] = static_cast<float>(m_frameRate);
    return true;
  }
  return false;
}

bool Browser::setParam(const std::string &name, const float value[4]) {
  if (name == "zoom") {
    setZoom(value[0]);
    return true;
  }
  if (name == "transparent") {
    setTransparent(value[0] > 0.5f);
    return true;
  }
  if (name == "inputEnabled") {
    setInputEnabled(value[0] > 0.5f);
    return true;
  }
  if (name == "frameRate") {
    setFrameRate(static_cast<int>(value[0]));
    return true;
  }
  return false;
}

// -----------------------------------------------------------------------------
// Private Methods
// -----------------------------------------------------------------------------

void Browser::createFallbackTexture(Context &ctx) {
  // Create a simple checkerboard pattern to indicate missing/failed state
  const int width = 64;
  const int height = 64;
  std::vector<uint8_t> pixels(width * height * 4);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int idx = (y * width + x) * 4;
      bool checker = ((x / 8) + (y / 8)) % 2 == 0;
      pixels[idx + 0] = checker ? 64 : 32; // R
      pixels[idx + 1] = checker ? 64 : 32; // G
      pixels[idx + 2] = checker ? 64 : 32; // B
      pixels[idx + 3] = 255;               // A
    }
  }

  WGPUTextureDescriptor desc{};
  desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
               WGPUTextureUsage_CopySrc;
  desc.dimension = WGPUTextureDimension_2D;
  desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
  desc.format = WGPUTextureFormat_RGBA8Unorm;
  desc.mipLevelCount = 1;
  desc.sampleCount = 1;

  m_fallbackTexture = wgpuDeviceCreateTexture(ctx.device(), &desc);

  WGPUTexelCopyTextureInfo dst{};
  dst.texture = m_fallbackTexture;
  dst.mipLevel = 0;
  dst.origin = {0, 0, 0};
  dst.aspect = WGPUTextureAspect_All;

  WGPUTexelCopyBufferLayout layout{};
  layout.offset = 0;
  layout.bytesPerRow = width * 4;
  layout.rowsPerImage = height;

  WGPUExtent3D extent = {static_cast<uint32_t>(width),
                         static_cast<uint32_t>(height), 1};
  wgpuQueueWriteTexture(ctx.queue(), &dst, pixels.data(), pixels.size(),
                        &layout, &extent);

  WGPUTextureViewDescriptor viewDesc{};
  viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
  viewDesc.dimension = WGPUTextureViewDimension_2D;
  viewDesc.mipLevelCount = 1;
  viewDesc.arrayLayerCount = 1;

  m_fallbackView = wgpuTextureCreateView(m_fallbackTexture, &viewDesc);
}

} // namespace vivid::cef
