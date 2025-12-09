#pragma once

/**
 * @file context.h
 * @brief Runtime context passed to chain setup/update functions
 *
 * The Context provides access to:
 * - Time information (elapsed time, delta time, frame count)
 * - Window dimensions
 * - Input state (mouse, keyboard)
 * - WebGPU device and queue
 * - Operator registry for visualization
 * - Chain management (context owns the chain)
 */

#include <vivid/chain.h>

#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <string>
#include <map>
#include <vector>
#include <memory>

namespace vivid {
struct OperatorState;
class Operator;

/**
 * @brief Operator info for visualization
 */
struct OperatorInfo {
    std::string name;       ///< Display name
    Operator* op = nullptr; ///< Pointer to operator
};

/**
 * @brief Key state for a single key
 */
struct KeyState {
    bool pressed = false;   ///< True during the frame the key was pressed
    bool held = false;      ///< True while the key is held down
    bool released = false;  ///< True during the frame the key was released
};

/**
 * @brief Mouse button state
 */
struct MouseButtonState {
    bool pressed = false;   ///< True during the frame the button was pressed
    bool held = false;      ///< True while the button is held down
    bool released = false;  ///< True during the frame the button was released
};

/**
 * @brief Runtime context providing access to time, input, and GPU resources
 *
 * Context is passed to setup() and update() functions in your chain.cpp.
 * Use it to access timing information, input state, and GPU resources.
 *
 * @par Example
 * @code
 * void update(Context& ctx) {
 *     float t = ctx.time();  // seconds since start
 *     float dt = ctx.dt();   // delta time
 *
 *     if (ctx.key(GLFW_KEY_SPACE).pressed) {
 *         // Space was just pressed this frame
 *     }
 *
 *     chain->process();
 * }
 * @endcode
 */
class Context {
public:
    /**
     * @brief Construct a context
     * @param window GLFW window handle
     * @param device WebGPU device
     * @param queue WebGPU queue
     */
    Context(GLFWwindow* window, WGPUDevice device, WGPUQueue queue);
    ~Context();

    /// @brief Called each frame before update
    void beginFrame();

    /// @brief Called each frame after update
    void endFrame();

    // -------------------------------------------------------------------------
    /// @name Time
    /// @{

    /**
     * @brief Get time since program start
     * @return Elapsed time in seconds
     */
    double time() const { return m_time; }

    /**
     * @brief Get time since last frame
     * @return Delta time in seconds
     */
    double dt() const { return m_dt; }

    /**
     * @brief Get current frame number
     * @return Frame count (0-indexed)
     */
    uint64_t frame() const { return m_frame; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Window
    /// @{

    /**
     * @brief Get window width
     * @return Width in pixels
     */
    int width() const { return m_width; }

    /**
     * @brief Get window height
     * @return Height in pixels
     */
    int height() const { return m_height; }

    /**
     * @brief Get aspect ratio
     * @return Width divided by height
     */
    float aspect() const { return m_height > 0 ? static_cast<float>(m_width) / m_height : 1.0f; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Mouse
    /// @{

    /**
     * @brief Get mouse position in pixels
     * @return Position with (0,0) at top-left
     */
    glm::vec2 mouse() const { return m_mousePos; }

    /**
     * @brief Get normalized mouse position
     * @return Position in range (-1 to 1) with Y up
     */
    glm::vec2 mouseNorm() const;

    /**
     * @brief Get mouse button state
     * @param button Button index (0=left, 1=right, 2=middle)
     * @return Button state struct
     */
    const MouseButtonState& mouseButton(int button) const;

    /**
     * @brief Get mouse scroll delta
     * @return Scroll amount since last frame
     */
    glm::vec2 scroll() const { return m_scroll; }

    /**
     * @brief Add scroll delta (called by scroll callback)
     * @param x Horizontal scroll
     * @param y Vertical scroll
     */
    void addScroll(float x, float y) { m_scroll.x += x; m_scroll.y += y; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Keyboard
    /// @{

    /**
     * @brief Get key state
     * @param keyCode GLFW key code (e.g., GLFW_KEY_SPACE)
     * @return Key state struct
     */
    const KeyState& key(int keyCode) const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name WebGPU Access
    /// @{

    /// @brief Get WebGPU device
    WGPUDevice device() const { return m_device; }

    /// @brief Get WebGPU queue
    WGPUQueue queue() const { return m_queue; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Output Texture
    /// @{

    /**
     * @brief Set the output texture (called by chain)
     * @param texture Texture view to display
     */
    void setOutputTexture(WGPUTextureView texture) { m_outputTexture = texture; }

    /**
     * @brief Get the output texture (read by display)
     * @return Current output texture view
     */
    WGPUTextureView outputTexture() const { return m_outputTexture; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Error State
    /// @{

    /// @brief Check if an error has occurred
    bool hasError() const { return !m_errorMessage.empty(); }

    /// @brief Get the error message
    const std::string& errorMessage() const { return m_errorMessage; }

    /// @brief Set an error message
    void setError(const std::string& message) { m_errorMessage = message; }

    /// @brief Clear the error state
    void clearError() { m_errorMessage.clear(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Chain Path
    /// @{

    /// @brief Set the chain source file path (for sidecar files)
    void setChainPath(const std::string& path) { m_chainPath = path; }

    /// @brief Get the chain source file path
    const std::string& chainPath() const { return m_chainPath; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Registry
    /// @{

    /**
     * @brief Register an operator for chain visualization
     * @param name Display name for the operator
     * @param op Pointer to the operator
     *
     * Registered operators appear in the chain visualizer (Tab key).
     */
    void registerOperator(const std::string& name, Operator* op);

    /**
     * @brief Get all registered operators
     * @return Vector of operator info structs
     */
    const std::vector<OperatorInfo>& registeredOperators() const { return m_operators; }

    /// @brief Clear all registered operators (called on hot-reload)
    void clearRegisteredOperators() { m_operators.clear(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Chain Access
    /// @{

    /**
     * @brief Get the chain (creates one if needed)
     * @return Reference to the chain
     *
     * The context owns the chain. Use this in setup() to configure operators:
     * @code
     * void setup(Context& ctx) {
     *     auto& chain = ctx.chain();
     *     chain.add<Noise>("noise");
     *     chain.output("noise");
     * }
     * @endcode
     */
    Chain& chain();

    /// @brief Get the chain (const version)
    const Chain& chain() const;

    /// @brief Check if a chain exists
    bool hasChain() const { return m_chain != nullptr; }

    /// @brief Reset the chain (called by core before setup)
    void resetChain();

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Preservation
    /// @{

    /**
     * @brief Save states from a chain before hot-reload
     * @param chain Chain to save states from
     */
    void preserveStates(Chain& chain);

    /**
     * @brief Restore states to a chain after hot-reload
     * @param chain Chain to restore states to
     */
    void restoreStates(Chain& chain);

    /// @brief Check if there are preserved states waiting
    bool hasPreservedStates() const { return !m_preservedStates.empty(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Display Settings
    /// @{

    /**
     * @brief Enable or disable vsync
     * @param enabled True for vsync on (Fifo), false for off (Immediate)
     *
     * Changes take effect on the next frame.
     */
    void vsync(bool enabled) {
        if (m_vsync != enabled) {
            m_vsync = enabled;
            m_vsyncChanged = true;
        }
    }

    /// @brief Get current vsync setting
    bool vsync() const { return m_vsync; }

    /// @brief Check if vsync setting changed (consumed by runtime)
    bool vsyncChanged() { bool c = m_vsyncChanged; m_vsyncChanged = false; return c; }

    /**
     * @brief Enable or disable fullscreen mode
     * @param enabled True for fullscreen, false for windowed
     *
     * Changes take effect on the next frame.
     */
    void fullscreen(bool enabled) {
        if (m_fullscreen != enabled) {
            m_fullscreen = enabled;
            m_fullscreenChanged = true;
        }
    }

    /// @brief Get current fullscreen setting
    bool fullscreen() const { return m_fullscreen; }

    /// @brief Check if fullscreen setting changed (consumed by runtime)
    bool fullscreenChanged() { bool c = m_fullscreenChanged; m_fullscreenChanged = false; return c; }

    /// @}

private:
    GLFWwindow* m_window;
    WGPUDevice m_device;
    WGPUQueue m_queue;

    // Time
    double m_time = 0.0;
    double m_dt = 0.0;
    double m_lastTime = 0.0;
    uint64_t m_frame = 0;

    // Window
    int m_width = 0;
    int m_height = 0;

    // Mouse
    glm::vec2 m_mousePos = {0, 0};
    glm::vec2 m_lastMousePos = {0, 0};
    glm::vec2 m_scroll = {0, 0};
    MouseButtonState m_mouseButtons[3];
    bool m_mouseButtonPrev[3] = {false, false, false};

    // Keyboard
    static constexpr int MAX_KEYS = GLFW_KEY_LAST + 1;
    KeyState m_keys[MAX_KEYS];
    bool m_keyPrev[MAX_KEYS] = {};

    // Output
    WGPUTextureView m_outputTexture = nullptr;

    // Error
    std::string m_errorMessage;

    // Chain path (for sidecar files)
    std::string m_chainPath;

    // Operator registry
    std::vector<OperatorInfo> m_operators;

    // Preserved states
    std::map<std::string, std::unique_ptr<OperatorState>> m_preservedStates;

    // Chain (owned by context)
    std::unique_ptr<Chain> m_chain;

    // Display settings
    bool m_vsync = true;
    bool m_vsyncChanged = false;
    bool m_fullscreen = false;
    bool m_fullscreenChanged = false;

    // Default states
    static const KeyState s_defaultKeyState;
    static const MouseButtonState s_defaultMouseState;
};

} // namespace vivid
