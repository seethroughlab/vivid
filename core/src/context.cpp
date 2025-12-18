// Vivid - Context Implementation

#include <vivid/context.h>
#include <vivid/chain.h>
#include <cstring>

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

} // namespace vivid
