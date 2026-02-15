// Vivid - Context Implementation

#include <vivid/context.h>
#include <vivid/chain.h>
#include <vivid/asset_loader.h>
#include <vivid/window_manager.h>
#include <vivid/video_exporter.h>
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

Context::Context(WGPUDevice device, WGPUQueue queue, int width, int height)
    : m_window(nullptr)  // Headless mode
    , m_device(device)
    , m_queue(queue)
{
    // Set dimensions
    m_width = width;
    m_height = height;
    m_renderWidth = width;
    m_renderHeight = height;
    m_renderResolutionSet = true;

    // Initialize time to 0 (will be updated via injectDeltaTime)
    m_time = 0.0;
    m_lastTime = 0.0;
    m_dt = 0.0;

    // Initialize mouse position
    m_mousePos = {0, 0};
    m_lastMousePos = {0, 0};

    // Initialize key states
    std::memset(m_keyPrev, 0, sizeof(m_keyPrev));
}

Context::~Context() {
    // Nothing to clean up - we don't own the resources
}

void Context::beginFrame() {
    // Handle windowed vs headless mode
    if (m_window) {
        // Update time from GLFW
        double now = glfwGetTime();
        m_dt = now - m_lastTime;
        m_lastTime = now;
        m_time = now;

        // Update window size and detect resizes
        int prevWidth = m_width;
        int prevHeight = m_height;
        if (m_renderResolutionSet) {
            // When an explicit render resolution is set (e.g. --resolution flag),
            // use it instead of the framebuffer size so operators allocate textures
            // at the requested resolution rather than the window/DPI-scaled size.
            m_width = m_renderWidth;
            m_height = m_renderHeight;
        } else {
            glfwGetFramebufferSize(m_window, &m_width, &m_height);
        }
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
    } else {
        // Headless mode: time and input are injected externally
        // Just update last mouse position for delta calculation
        m_lastMousePos = m_mousePos;

        // Update pressed/released states for injected input
        for (int i = 0; i < 3; ++i) {
            bool current = m_mouseButtonPrev[i];  // Current state from injection
            m_mouseButtons[i].pressed = current && !m_mouseButtonPrevFrame[i];
            m_mouseButtons[i].released = !current && m_mouseButtonPrevFrame[i];
            m_mouseButtons[i].held = current;
            m_mouseButtonPrevFrame[i] = current;
        }

        for (int i = 0; i < MAX_KEYS; ++i) {
            bool current = m_keyPrev[i];  // Current state from injection
            m_keys[i].pressed = current && !m_keyPrevFrame[i];
            m_keys[i].released = !current && m_keyPrevFrame[i];
            m_keys[i].held = current;
            m_keyPrevFrame[i] = current;
        }
    }

    // Clear output texture for this frame
    m_outputTexture = nullptr;
}

void Context::endFrame() {
    // Reset per-frame scroll
    m_scroll = {0, 0};

    // Clear per-frame character input
    m_characterInput.clear();

    // Increment frame counter
    ++m_frame;
}

glm::vec2 Context::mouseNorm() const {
    if (m_width <= 0 || m_height <= 0) return {0, 0};

    // In headless mode, use render dimensions directly
    if (!m_window) {
        return {
            m_mousePos.x / m_width,
            m_mousePos.y / m_height
        };
    }

    // Mouse position from GLFW is in window coordinates, not framebuffer coordinates.
    // On HiDPI/Retina displays, framebuffer is 2x larger than window.
    // Get window size for proper normalization.
    int windowW, windowH;
    glfwGetWindowSize(m_window, &windowW, &windowH);
    if (windowW <= 0 || windowH <= 0) return {0, 0};

    // Returns 0-1 normalized coordinates, Y-down (matches UV, Shape, Canvas)
    return {
        m_mousePos.x / windowW,
        m_mousePos.y / windowH
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

// =============================================================================
// Snapshot
// =============================================================================

std::string Context::snapshot(const std::string& filename) {
    namespace fs = std::filesystem;

    // Get output texture from chain
    WGPUTexture tex = m_chain ? m_chain->outputTexture() : nullptr;
    if (!tex) {
        printf("[Context] Snapshot failed: no output texture\n");
        return "";
    }

    std::string outputPath = filename;

    // Auto-generate filename if not provided
    if (outputPath.empty()) {
        // Use project directory
        std::string projectDir = ".";
        if (!m_chainPath.empty()) {
            fs::path p(m_chainPath);
            if (p.has_parent_path()) {
                projectDir = p.parent_path().string();
            }
        }

        // Generate unique filename
        static int snapshotNum = 1;
        do {
            outputPath = projectDir + "/snapshot_" + std::to_string(snapshotNum) + ".png";
            snapshotNum++;
        } while (fs::exists(outputPath) && snapshotNum < 10000);
    }

    // Save the snapshot
    if (VideoExporter::saveSnapshot(m_device, m_queue, tex, outputPath)) {
        printf("[Snapshot] Saved: %s\n", outputPath.c_str());
        return outputPath;
    } else {
        printf("[Snapshot] Failed to save: %s\n", outputPath.c_str());
        return "";
    }
}

// =============================================================================
// Input Injection (for headless/embedded use)
// =============================================================================

void Context::injectMousePosition(float x, float y) {
    m_mousePos = {x, y};
}

void Context::injectMouseButton(int button, bool pressed) {
    if (button >= 0 && button < 3) {
        m_mouseButtonPrev[button] = pressed;
    }
}

void Context::injectKeyState(int keycode, bool pressed) {
    if (keycode >= 0 && keycode < MAX_KEYS) {
        m_keyPrev[keycode] = pressed;
    }
}

void Context::injectScroll(float dx, float dy) {
    m_scroll.x += dx;
    m_scroll.y += dy;
}

void Context::injectDeltaTime(double dt) {
    m_dt = dt;
    m_time += dt;
    ++m_frame;
}

} // namespace vivid
