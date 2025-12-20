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
#include <deque>
#include <memory>

namespace vivid {
struct OperatorState;
class Operator;
class WindowManager;

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
 * @brief Debug value with rolling history for visualization
 */
struct DebugValue {
    std::deque<float> history;  ///< Rolling buffer of values
    float current = 0.0f;       ///< Most recent value
    bool updatedThisFrame = false;  ///< Was this value updated this frame?
    int framesWithoutUpdate = 0;    ///< Frames since last update (for auto-cleanup)
    static constexpr size_t MAX_HISTORY = 120;  ///< ~2 seconds at 60fps
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
     *
     * When recording, returns deterministic time based on frame count and fps.
     */
    double time() const {
        if (m_recording && m_recordingFps > 0) {
            return static_cast<double>(m_frame) / m_recordingFps;
        }
        return m_time;
    }

    /**
     * @brief Get real wall-clock time (even during recording)
     * @return Actual elapsed time in seconds
     */
    double realTime() const { return m_time; }

    /**
     * @brief Get time since last frame
     * @return Delta time in seconds
     *
     * When recording, returns a fixed timestep (1/fps) for deterministic output.
     * Use this for all time-based calculations in operators.
     */
    double dt() const {
        if (m_recording) {
            return m_recordingFps > 0 ? 1.0 / m_recordingFps : m_dt;
        }
        return m_dt;
    }

    /**
     * @brief Get real delta time (always wall-clock, even during recording)
     * @return Actual time elapsed since last frame
     *
     * Use this sparingly - most operators should use dt() which is
     * deterministic during recording.
     */
    double realDt() const { return m_dt; }

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

    /**
     * @brief Get window X position
     * @return X position in screen coordinates
     */
    int windowX() const { return m_windowX; }

    /**
     * @brief Get window Y position
     * @return Y position in screen coordinates
     */
    int windowY() const { return m_windowY; }

    /**
     * @brief Move window to a specific position
     * @param x X position in screen coordinates
     * @param y Y position in screen coordinates
     *
     * Changes take effect on the next frame.
     */
    void setWindowPos(int x, int y) {
        m_targetWindowX = x;
        m_targetWindowY = y;
        m_windowPosChanged = true;
    }

    /// @brief Get target window X position for pending move
    int targetWindowX() const { return m_targetWindowX; }

    /// @brief Get target window Y position for pending move
    int targetWindowY() const { return m_targetWindowY; }

    /// @brief Consume window position change flag (returns true once, then false)
    bool consumeWindowPosChange() { bool c = m_windowPosChanged; m_windowPosChanged = false; return c; }

    /**
     * @brief Resize the window
     * @param w Width in pixels
     * @param h Height in pixels
     *
     * Changes take effect on the next frame.
     */
    void setWindowSize(int w, int h) {
        m_targetWindowWidth = w;
        m_targetWindowHeight = h;
        m_windowSizeChanged = true;
    }

    /// @brief Get target window width for pending resize
    int targetWindowWidth() const { return m_targetWindowWidth; }

    /// @brief Get target window height for pending resize
    int targetWindowHeight() const { return m_targetWindowHeight; }

    /// @brief Consume window size change flag (returns true once, then false)
    bool consumeWindowSizeChange() { bool c = m_windowSizeChanged; m_windowSizeChanged = false; return c; }

    /**
     * @brief Check if window was resized this frame
     * @return True if window dimensions changed since last frame
     *
     * This detects both user-initiated resizes and programmatic resizes.
     * Use this to update layouts or recreate resolution-dependent resources.
     */
    bool wasResized() const { return m_wasResized; }

    /// @brief Set resize flag (called by runtime)
    void setWasResized(bool resized) { m_wasResized = resized; }

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

    /**
     * @brief Get mouse movement delta
     * @return Change in mouse position since last frame (pixels)
     *
     * Useful for FPS-style camera controls or drag operations.
     */
    glm::vec2 mouseDelta() const { return m_mousePos - m_lastMousePos; }

    /**
     * @brief Get normalized mouse movement delta
     * @return Change in normalized position since last frame
     *
     * Returns delta in range roughly (-2 to 2) based on window dimensions.
     */
    glm::vec2 mouseDeltaNorm() const {
        if (m_width <= 0 || m_height <= 0) return {0, 0};
        glm::vec2 delta = m_mousePos - m_lastMousePos;
        return {
            (delta.x / m_width) * 2.0f,
            -(delta.y / m_height) * 2.0f  // Flip Y
        };
    }

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

    /**
     * @brief Check if Shift key is held
     * @return True if either left or right Shift is held
     */
    bool shiftHeld() const {
        return m_keys[GLFW_KEY_LEFT_SHIFT].held || m_keys[GLFW_KEY_RIGHT_SHIFT].held;
    }

    /**
     * @brief Check if Ctrl key is held
     * @return True if either left or right Ctrl is held
     */
    bool ctrlHeld() const {
        return m_keys[GLFW_KEY_LEFT_CONTROL].held || m_keys[GLFW_KEY_RIGHT_CONTROL].held;
    }

    /**
     * @brief Check if Alt key is held
     * @return True if either left or right Alt is held
     */
    bool altHeld() const {
        return m_keys[GLFW_KEY_LEFT_ALT].held || m_keys[GLFW_KEY_RIGHT_ALT].held;
    }

    /**
     * @brief Check if Super key is held (Cmd on Mac, Win key on Windows)
     * @return True if either left or right Super is held
     */
    bool superHeld() const {
        return m_keys[GLFW_KEY_LEFT_SUPER].held || m_keys[GLFW_KEY_RIGHT_SUPER].held;
    }

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
    /// @name Debug Values
    /// @{

    /**
     * @brief Record a debug value for visualization
     * @param name Display name for the value
     * @param value Float value to record
     *
     * Values are displayed in a debug panel with rolling history graphs.
     * Call each frame for values you want to visualize.
     *
     * @par Example
     * @code
     * void update(Context& ctx) {
     *     ctx.debug("lfo", sinf(ctx.time() * 2.0f));
     *     ctx.debug("envelope", levels.rms());
     * }
     * @endcode
     */
    void debug(const std::string& name, float value);

    /// @brief Record a bool value (converted to 0.0 or 1.0)
    void debug(const std::string& name, bool value) { debug(name, value ? 1.0f : 0.0f); }

    /// @brief Record a vec2 value (stores magnitude)
    void debug(const std::string& name, const glm::vec2& value) { debug(name, glm::length(value)); }

    /// @brief Record a vec3 value (stores magnitude)
    void debug(const std::string& name, const glm::vec3& value) { debug(name, glm::length(value)); }

    /// @brief Record an int value
    void debug(const std::string& name, int value) { debug(name, static_cast<float>(value)); }

    /// @brief Record a double value
    void debug(const std::string& name, double value) { debug(name, static_cast<float>(value)); }

    /// @brief Record an unsigned int value
    void debug(const std::string& name, uint32_t value) { debug(name, static_cast<float>(value)); }

    /// @brief Record a uint64_t value
    void debug(const std::string& name, uint64_t value) { debug(name, static_cast<float>(value)); }

    /**
     * @brief Get all debug values for rendering
     * @return Map of name to DebugValue
     */
    const std::map<std::string, DebugValue>& debugValues() const { return m_debugValues; }

    /**
     * @brief Prepare debug values for next frame
     *
     * Called at start of frame. Marks all values as not updated and
     * removes values that haven't been updated for several frames.
     */
    void beginDebugFrame();

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

    /// @brief Consume vsync change flag (returns true once, then false)
    bool consumeVsyncChange() { bool c = m_vsyncChanged; m_vsyncChanged = false; return c; }

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

    /// @brief Consume fullscreen change flag (returns true once, then false)
    bool consumeFullscreenChange() { bool c = m_fullscreenChanged; m_fullscreenChanged = false; return c; }

    /**
     * @brief Enable or disable borderless (undecorated) window mode
     * @param enabled True for borderless, false for decorated
     *
     * In borderless mode, the window has no title bar or borders.
     * Changes take effect on the next frame.
     */
    void borderless(bool enabled) {
        if (m_borderless != enabled) {
            m_borderless = enabled;
            m_borderlessChanged = true;
        }
    }

    /// @brief Get current borderless setting
    bool borderless() const { return m_borderless; }

    /// @brief Consume borderless change flag (returns true once, then false)
    bool consumeBorderlessChange() { bool c = m_borderlessChanged; m_borderlessChanged = false; return c; }

    /**
     * @brief Enable or disable always-on-top (floating) mode
     * @param enabled True for always-on-top, false for normal
     *
     * Changes take effect on the next frame.
     */
    void alwaysOnTop(bool enabled) {
        if (m_alwaysOnTop != enabled) {
            m_alwaysOnTop = enabled;
            m_alwaysOnTopChanged = true;
        }
    }

    /// @brief Get current always-on-top setting
    bool alwaysOnTop() const { return m_alwaysOnTop; }

    /// @brief Consume always-on-top change flag (returns true once, then false)
    bool consumeAlwaysOnTopChange() { bool c = m_alwaysOnTopChanged; m_alwaysOnTopChanged = false; return c; }

    /**
     * @brief Show or hide the mouse cursor
     * @param visible True to show cursor, false to hide
     *
     * Changes take effect on the next frame.
     */
    void cursorVisible(bool visible) {
        if (m_cursorVisible != visible) {
            m_cursorVisible = visible;
            m_cursorVisibleChanged = true;
        }
    }

    /// @brief Get current cursor visibility setting
    bool cursorVisible() const { return m_cursorVisible; }

    /// @brief Consume cursor visibility change flag (returns true once, then false)
    bool consumeCursorVisibleChange() { bool c = m_cursorVisibleChanged; m_cursorVisibleChanged = false; return c; }

    /**
     * @brief Get the number of connected monitors
     * @return Number of monitors
     */
    int monitorCount() const;

    /**
     * @brief Get the current monitor index
     * @return Index of monitor the window is on (0 = primary)
     */
    int currentMonitor() const;

    /**
     * @brief Move window to a specific monitor
     * @param index Monitor index (0 = primary)
     *
     * If in fullscreen mode, switches to fullscreen on the target monitor.
     * If in windowed mode, centers the window on the target monitor.
     * Changes take effect on the next frame.
     */
    void moveToMonitor(int index) {
        if (m_targetMonitor != index) {
            m_targetMonitor = index;
            m_monitorChanged = true;
        }
    }

    /// @brief Get target monitor for pending move
    int targetMonitor() const { return m_targetMonitor; }

    /// @brief Consume monitor change flag (returns true once, then false)
    bool consumeMonitorChange() { bool c = m_monitorChanged; m_monitorChanged = false; return c; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Recording Mode
    /// @{

    /**
     * @brief Set recording mode with target fps
     * @param recording True if recording video
     * @param fps Target video fps (used to calculate audio frames per video frame)
     *
     * When recording, audio operators should generate exactly (sampleRate/fps) frames
     * per video frame to maintain sync, rather than using wall-clock dt.
     */
    void setRecordingMode(bool recording, float fps = 60.0f) {
        m_recording = recording;
        m_recordingFps = fps;
    }

    /// @brief Check if in recording mode
    bool isRecording() const { return m_recording; }

    /// @brief Get recording fps (for audio sync calculation)
    float recordingFps() const { return m_recordingFps; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Render Resolution
    /// @{

    /**
     * @brief Get render width (texture resolution)
     * @return Render width in pixels
     *
     * This is the resolution operators render at, which can differ from window size.
     * Defaults to 1280 or the value set via setRenderResolution().
     */
    int renderWidth() const { return m_renderWidth; }

    /**
     * @brief Get render height (texture resolution)
     * @return Render height in pixels
     */
    int renderHeight() const { return m_renderHeight; }

    /**
     * @brief Get render aspect ratio
     * @return Render width divided by height
     */
    float renderAspect() const {
        return m_renderHeight > 0 ? static_cast<float>(m_renderWidth) / m_renderHeight : 1.0f;
    }

    /**
     * @brief Set render resolution (texture size)
     * @param w Width in pixels
     * @param h Height in pixels
     *
     * Called by runtime based on --render command-line arg or chain.resolution().
     */
    void setRenderResolution(int w, int h) {
        m_renderWidth = w;
        m_renderHeight = h;
        m_renderResolutionSet = true;
    }

    /**
     * @brief Check if render resolution was explicitly set
     * @return True if setRenderResolution() was called
     */
    bool hasRenderResolution() const { return m_renderResolutionSet; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Audio Timing
    /// @{

    /**
     * @brief Set number of audio frames to generate this frame
     * @param frames Number of audio frames needed based on elapsed time
     *
     * Called by Chain::process() to tell audio operators how many
     * samples to generate this frame to maintain 48kHz output rate.
     */
    void setAudioFramesThisFrame(uint32_t frames) { m_audioFramesThisFrame = frames; }

    /**
     * @brief Get number of audio frames to generate this frame
     * @return Number of frames based on elapsed time (typically 800 at 60fps)
     *
     * Audio operators should generate this many frames of audio to
     * maintain consistent 48kHz output regardless of video frame rate.
     */
    uint32_t audioFramesThisFrame() const { return m_audioFramesThisFrame; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name GPU Frame Encoder (Command Buffer Batching)
    /// @{

    /**
     * @brief Begin GPU frame - creates shared command encoder
     *
     * Called by Chain::process() at the start. All operators should use
     * gpuEncoder() to get the shared encoder instead of creating their own.
     * This reduces GPU driver overhead by batching all work into one submit.
     */
    void beginGpuFrame();

    /**
     * @brief End GPU frame - submits the batched command buffer
     *
     * Called by Chain::process() at the end. Finishes and submits the
     * command buffer, then releases the encoder.
     */
    void endGpuFrame();

    /**
     * @brief Get the current GPU command encoder
     * @return The shared command encoder for this frame
     *
     * Operators should use this instead of creating their own encoder.
     * Returns nullptr if called outside of a GPU frame (between beginGpuFrame/endGpuFrame).
     */
    WGPUCommandEncoder gpuEncoder() const { return m_gpuEncoder; }

    /**
     * @brief Check if a GPU frame is active
     * @return True if between beginGpuFrame() and endGpuFrame()
     */
    bool hasActiveGpuEncoder() const { return m_gpuEncoderActive; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Multi-Window Support
    /// @{

    /**
     * @brief Set the WindowManager (called by runtime)
     * @param wm WindowManager pointer
     */
    void setWindowManager(WindowManager* wm) { m_windowManager = wm; }

    /**
     * @brief Get the WindowManager
     * @return WindowManager pointer, or nullptr if not set
     */
    WindowManager* windowManager() const { return m_windowManager; }

    /**
     * @brief Create a secondary output window
     * @param monitorIndex Monitor to create on (-1 = primary monitor)
     * @return Window handle for future operations, or -1 on failure
     *
     * Secondary windows display the chain output or a specific operator's output.
     * Use setOutputWindowSource() to route different operators to different windows.
     *
     * @par Example
     * @code
     * int projector = ctx.createOutputWindow(1);  // Second monitor
     * ctx.setOutputWindowFullscreen(projector, true);
     * @endcode
     */
    int createOutputWindow(int monitorIndex = -1);

    /**
     * @brief Destroy a secondary output window
     * @param handle Window handle from createOutputWindow()
     */
    void destroyOutputWindow(int handle);

    /**
     * @brief Set output window position
     * @param handle Window handle
     * @param x X position in screen coordinates
     * @param y Y position in screen coordinates
     */
    void setOutputWindowPos(int handle, int x, int y);

    /**
     * @brief Set output window size
     * @param handle Window handle
     * @param w Width in pixels
     * @param h Height in pixels
     */
    void setOutputWindowSize(int handle, int w, int h);

    /**
     * @brief Set output window fullscreen mode
     * @param handle Window handle
     * @param fullscreen True for fullscreen, false for windowed
     * @param monitorIndex Target monitor (-1 = current)
     */
    void setOutputWindowFullscreen(int handle, bool fullscreen, int monitorIndex = -1);

    /**
     * @brief Set which operator this window displays
     * @param handle Window handle
     * @param operatorName Operator name, or empty string for chain output
     *
     * @par Example
     * @code
     * ctx.setOutputWindowSource(projector, "blur");  // Show blur operator
     * ctx.setOutputWindowSource(ledWall, "");        // Show chain output
     * @endcode
     */
    void setOutputWindowSource(int handle, const std::string& operatorName);

    /**
     * @brief Get number of output windows (including primary)
     * @return Window count
     */
    int outputWindowCount() const;

    // === Span Mode ===

    /**
     * @brief Enable span mode across multiple monitors
     * @param columns Number of horizontal monitors
     * @param rows Number of vertical monitors
     *
     * In span mode, the chain renders at the combined resolution and
     * each monitor shows a portion of the output.
     *
     * @par Example - 2x1 horizontal span
     * @code
     * ctx.enableSpanMode(2, 1);  // Two monitors side-by-side
     * @endcode
     */
    void enableSpanMode(int columns, int rows);

    /**
     * @brief Disable span mode
     */
    void disableSpanMode();

    /**
     * @brief Check if span mode is active
     * @return True if span mode is enabled
     */
    bool isSpanMode() const;

    /**
     * @brief Get total span resolution
     * @return Combined resolution across all monitors
     */
    glm::ivec2 spanResolution() const;

    /**
     * @brief Set bezel gap compensation
     * @param hPixels Horizontal gap between monitors in pixels
     * @param vPixels Vertical gap between monitors in pixels
     *
     * Bezel compensation accounts for the physical gap between monitor
     * bezels, ensuring continuous imagery across the span.
     */
    void setSpanBezelGap(int hPixels, int vPixels);

    /**
     * @brief Auto-configure span based on detected monitors
     *
     * Detects connected monitors and creates/positions borderless
     * fullscreen windows across them automatically.
     */
    void autoConfigureSpan();

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
    int m_windowX = 0;
    int m_windowY = 0;
    int m_targetWindowX = 0;
    int m_targetWindowY = 0;
    bool m_windowPosChanged = false;
    int m_targetWindowWidth = 0;
    int m_targetWindowHeight = 0;
    bool m_windowSizeChanged = false;
    bool m_wasResized = false;

    // Render resolution (can differ from window size)
    int m_renderWidth = 1280;
    int m_renderHeight = 720;
    bool m_renderResolutionSet = false;

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

    // Debug values
    std::map<std::string, DebugValue> m_debugValues;

    // Preserved states
    std::map<std::string, std::unique_ptr<OperatorState>> m_preservedStates;

    // Chain (owned by context)
    std::unique_ptr<Chain> m_chain;

    // Display settings
    bool m_vsync = true;
    bool m_vsyncChanged = false;
    bool m_fullscreen = false;
    bool m_fullscreenChanged = false;
    bool m_borderless = false;
    bool m_borderlessChanged = false;
    bool m_alwaysOnTop = false;
    bool m_alwaysOnTopChanged = false;
    bool m_cursorVisible = true;
    bool m_cursorVisibleChanged = false;
    int m_targetMonitor = 0;
    bool m_monitorChanged = false;

    // Recording mode
    bool m_recording = false;
    float m_recordingFps = 60.0f;

    // Audio timing
    uint32_t m_audioFramesThisFrame = 1024;  // Default to block size

    // Multi-window support
    WindowManager* m_windowManager = nullptr;

    // GPU frame encoder (command buffer batching)
    WGPUCommandEncoder m_gpuEncoder = nullptr;
    bool m_gpuEncoderActive = false;

    // Default states
    static const KeyState s_defaultKeyState;
    static const MouseButtonState s_defaultMouseState;
};

} // namespace vivid
