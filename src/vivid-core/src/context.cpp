// Vivid - Context Implementation

#include <vivid/context.h>
#include <vivid/chain.h>
#include <vivid/asset_loader.h>
#include <vivid/window_manager.h>
#include <cstring>
#include <filesystem>

namespace vivid {

// Static default states
const KeyState Context::s_defaultKeyState = {};
const MouseButtonState Context::s_defaultMouseState = {};

Context::Context(GLFWwindow* window, WGPUDevice device, WGPUQueue queue)
    : m_window(window)
    , m_device(device)
    , m_queue(queue)
{
    m_lastTime = glfwGetTime();

    // Get initial window size
    glfwGetFramebufferSize(window, &m_width, &m_height);

    // Get initial window position
    glfwGetWindowPos(window, &m_windowX, &m_windowY);

    // Get initial mouse position
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    m_mousePos = {static_cast<float>(mx), static_cast<float>(my)};
    m_lastMousePos = m_mousePos;

    // Initialize key states
    std::memset(m_keyPrev, 0, sizeof(m_keyPrev));
}

Context::~Context() {
    // Nothing to clean up - we don't own the resources
}

void Context::beginFrame() {
    // Update time
    double now = glfwGetTime();
    m_dt = now - m_lastTime;
    m_lastTime = now;
    m_time = now;

    // Update window size and detect resizes
    int prevWidth = m_width;
    int prevHeight = m_height;
    glfwGetFramebufferSize(m_window, &m_width, &m_height);
    m_wasResized = (m_width != prevWidth || m_height != prevHeight);

    // Update window position
    glfwGetWindowPos(m_window, &m_windowX, &m_windowY);

    // Update mouse position
    double mx, my;
    glfwGetCursorPos(m_window, &mx, &my);
    m_lastMousePos = m_mousePos;
    m_mousePos = {static_cast<float>(mx), static_cast<float>(my)};

    // Update mouse buttons
    for (int i = 0; i < 3; ++i) {
        bool current = glfwGetMouseButton(m_window, i) == GLFW_PRESS;
        m_mouseButtons[i].pressed = current && !m_mouseButtonPrev[i];
        m_mouseButtons[i].released = !current && m_mouseButtonPrev[i];
        m_mouseButtons[i].held = current;
        m_mouseButtonPrev[i] = current;
    }

    // Update keyboard
    for (int i = 0; i < MAX_KEYS; ++i) {
        bool current = glfwGetKey(m_window, i) == GLFW_PRESS;
        m_keys[i].pressed = current && !m_keyPrev[i];
        m_keys[i].released = !current && m_keyPrev[i];
        m_keys[i].held = current;
        m_keyPrev[i] = current;
    }

    // Clear output texture for this frame
    m_outputTexture = nullptr;
}

void Context::endFrame() {
    // Reset per-frame scroll
    m_scroll = {0, 0};

    // Increment frame counter
    ++m_frame;
}

glm::vec2 Context::mouseNorm() const {
    if (m_width <= 0 || m_height <= 0) return {0, 0};

    return {
        (m_mousePos.x / m_width) * 2.0f - 1.0f,
        1.0f - (m_mousePos.y / m_height) * 2.0f  // Flip Y
    };
}

const MouseButtonState& Context::mouseButton(int button) const {
    if (button < 0 || button >= 3) return s_defaultMouseState;
    return m_mouseButtons[button];
}

const KeyState& Context::key(int keyCode) const {
    if (keyCode < 0 || keyCode >= MAX_KEYS) return s_defaultKeyState;
    return m_keys[keyCode];
}

void Context::preserveStates(Chain& chain) {
    m_preservedStates = chain.saveAllStates();
}

void Context::restoreStates(Chain& chain) {
    if (!m_preservedStates.empty()) {
        chain.restoreAllStates(m_preservedStates);
        m_preservedStates.clear();
    }
}

void Context::registerOperator(const std::string& name, Operator* op) {
    m_operators.push_back({name, op});
}

Chain& Context::chain() {
    if (!m_chain) {
        m_chain = std::make_unique<Chain>();
    }
    return *m_chain;
}

const Chain& Context::chain() const {
    if (!m_chain) {
        // This shouldn't happen in normal use, but handle it gracefully
        const_cast<Context*>(this)->m_chain = std::make_unique<Chain>();
    }
    return *m_chain;
}

void Context::resetChain() {
    m_chain = std::make_unique<Chain>();
}

void Context::addAssetPath(const std::string& name, const std::string& path) {
    namespace fs = std::filesystem;
    fs::path resolvedPath = path;

    // Resolve relative paths from project directory
    if (!resolvedPath.is_absolute()) {
        resolvedPath = AssetLoader::instance().projectDir() / path;
    }

    AssetLoader::instance().registerAssetPath(name, resolvedPath);
}

int Context::monitorCount() const {
    int count = 0;
    glfwGetMonitors(&count);
    return count;
}

int Context::currentMonitor() const {
    if (!m_window) return 0;

    // Get window position
    int wx, wy;
    glfwGetWindowPos(m_window, &wx, &wy);

    // Get window size
    int ww, wh;
    glfwGetWindowSize(m_window, &ww, &wh);

    // Window center
    int wcx = wx + ww / 2;
    int wcy = wy + wh / 2;

    // Find which monitor contains the window center
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);

    for (int i = 0; i < count; ++i) {
        int mx, my;
        glfwGetMonitorPos(monitors[i], &mx, &my);

        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (!mode) continue;

        // Check if window center is within this monitor
        if (wcx >= mx && wcx < mx + mode->width &&
            wcy >= my && wcy < my + mode->height) {
            return i;
        }
    }

    return 0;  // Default to primary
}

// =============================================================================
// Multi-Window Support
// =============================================================================

int Context::createOutputWindow(int monitorIndex) {
    if (!m_windowManager) return -1;
    return m_windowManager->createOutputWindow(monitorIndex, true);
}

void Context::destroyOutputWindow(int handle) {
    if (m_windowManager) {
        m_windowManager->destroyOutputWindow(handle);
    }
}

void Context::setOutputWindowPos(int handle, int x, int y) {
    if (m_windowManager) {
        m_windowManager->setWindowPos(handle, x, y);
    }
}

void Context::setOutputWindowSize(int handle, int w, int h) {
    if (m_windowManager) {
        m_windowManager->setWindowSize(handle, w, h);
    }
}

void Context::setOutputWindowFullscreen(int handle, bool fullscreen, int monitorIndex) {
    if (m_windowManager) {
        m_windowManager->setWindowFullscreen(handle, fullscreen, monitorIndex);
    }
}

void Context::setOutputWindowSource(int handle, const std::string& operatorName) {
    if (m_windowManager) {
        m_windowManager->setWindowSource(handle, operatorName);
    }
}

int Context::outputWindowCount() const {
    if (!m_windowManager) return 1;  // Primary window only
    return m_windowManager->windowCount();
}

void Context::enableSpanMode(int columns, int rows) {
    if (m_windowManager) {
        m_windowManager->enableSpanMode(columns, rows);
    }
}

void Context::disableSpanMode() {
    if (m_windowManager) {
        m_windowManager->disableSpanMode();
    }
}

bool Context::isSpanMode() const {
    if (!m_windowManager) return false;
    return m_windowManager->isSpanMode();
}

glm::ivec2 Context::spanResolution() const {
    if (!m_windowManager) return {0, 0};
    return m_windowManager->spanResolution();
}

void Context::setSpanBezelGap(int hPixels, int vPixels) {
    if (m_windowManager) {
        m_windowManager->setBezelGap(hPixels, vPixels);
    }
}

void Context::autoConfigureSpan() {
    if (m_windowManager) {
        m_windowManager->autoConfigureSpan();
    }
}

// =============================================================================
// GPU Frame Encoder (Command Buffer Batching)
// =============================================================================

void Context::beginGpuFrame() {
    if (m_gpuEncoderActive) {
        // Already have an active encoder - this shouldn't happen
        return;
    }

    WGPUCommandEncoderDescriptor desc = {};
    m_gpuEncoder = wgpuDeviceCreateCommandEncoder(m_device, &desc);
    m_gpuEncoderActive = true;
}

void Context::endGpuFrame() {
    if (!m_gpuEncoderActive || !m_gpuEncoder) {
        return;
    }

    // Finish and submit the command buffer
    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(m_gpuEncoder, &cmdDesc);
    wgpuQueueSubmit(m_queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(m_gpuEncoder);

    m_gpuEncoder = nullptr;
    m_gpuEncoderActive = false;
}

// =============================================================================
// Debug Values
// =============================================================================

void Context::debug(const std::string& name, float value) {
    auto& dv = m_debugValues[name];
    dv.current = value;
    dv.updatedThisFrame = true;
    dv.framesWithoutUpdate = 0;

    // Add to history
    dv.history.push_back(value);
    if (dv.history.size() > DebugValue::MAX_HISTORY) {
        dv.history.pop_front();
    }
}

void Context::beginDebugFrame() {
    // Mark all values as not updated and increment stale counter
    for (auto it = m_debugValues.begin(); it != m_debugValues.end(); ) {
        it->second.updatedThisFrame = false;
        it->second.framesWithoutUpdate++;

        // Remove values that haven't been updated for 60 frames (1 second)
        if (it->second.framesWithoutUpdate > 60) {
            it = m_debugValues.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace vivid
