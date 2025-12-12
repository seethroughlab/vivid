#pragma once

/**
 * @file chain.h
 * @brief Chain API for managing operator graphs
 *
 * Chain manages a collection of operators with automatic dependency resolution
 * and state preservation across hot-reloads.
 */

#include <vivid/operator.h>
#include <vivid/audio_graph.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <map>
#include <stdexcept>
#include <iostream>

namespace vivid {

class Context;
class AudioOperator;
class AudioOutput;
struct AudioBuffer;

/**
 * @brief Manages an operator graph with dependency resolution
 *
 * Chain is the primary way to build vivid projects. Add operators with add<T>(),
 * connect them with input(), and call process() each frame.
 *
 * @par Example
 * @code
 * Chain* chain = nullptr;
 *
 * void setup(Context& ctx) {
 *     delete chain;
 *     chain = new Chain(ctx, 1280, 720);
 *
 *     chain->add<Noise>("noise").scale(4.0f);
 *     chain->add<HSV>("color").input("noise").hueShift(0.3f);
 *     chain->add<Output>("out").input("color");
 * }
 *
 * void update(Context& ctx) {
 *     chain->process();
 * }
 * @endcode
 */
class Chain {
public:
    Chain() = default;
    ~Chain() = default;

    // Non-copyable and non-movable (AudioGraph is non-movable)
    Chain(const Chain&) = delete;
    Chain& operator=(const Chain&) = delete;
    Chain(Chain&&) = delete;
    Chain& operator=(Chain&&) = delete;

    /**
     * @brief Add an operator to the chain (internal - do not call directly)
     * @param name Unique name for this operator
     * @param op Operator to add (takes ownership)
     * @return Raw pointer to the added operator
     *
     * This is the non-template implementation that lives in vivid.exe.
     * Use add<T>() instead which wraps this.
     */
    Operator* addOperator(const std::string& name, Operator* op);

    /**
     * @brief Add an operator to the chain
     * @tparam T Operator type (e.g., Noise, Blur, Output)
     * @tparam Args Constructor argument types
     * @param name Unique name for this operator
     * @param args Constructor arguments forwarded to T
     * @return Reference to the new operator for method chaining
     *
     * @par Example
     * @code
     * chain->add<Noise>("noise").scale(4.0f).speed(0.5f);
     * chain->add<Blur>("blur").input("noise").radius(5.0f);
     * @endcode
     */
    template<typename T, typename... Args>
    T& add(const std::string& name, Args&&... args) {
        // Create operator in caller's context (the DLL)
        T* op = new T(std::forward<Args>(args)...);
        // Transfer to exe's memory management via non-template function
        addOperator(name, op);
        return *op;
    }

    /**
     * @brief Get an operator by name with type checking
     * @tparam T Expected operator type
     * @param name Operator name
     * @return Reference to the operator
     * @throw std::runtime_error if not found or type mismatch
     *
     * @par Example
     * @code
     * chain->get<Noise>("noise").scale(8.0f);  // Modify existing operator
     * @endcode
     */
    template<typename T>
    T& get(const std::string& name) {
        auto it = operators_.find(name);
        if (it == operators_.end()) {
            throw std::runtime_error("Operator not found: " + name);
        }
        T* typed = dynamic_cast<T*>(it->second.get());
        if (!typed) {
            throw std::runtime_error("Operator type mismatch: " + name);
        }
        return *typed;
    }

    /**
     * @brief Get operator by name (untyped)
     * @param name Operator name
     * @return Pointer to operator, or nullptr if not found
     */
    Operator* getByName(const std::string& name);

    /**
     * @brief Get name of an operator
     * @param op Pointer to operator
     * @return Operator name, or empty string if not found
     */
    std::string getName(Operator* op) const;

    /**
     * @brief Specify which operator provides the final output
     * @param name Name of the output operator
     *
     * This is the recommended way to specify output (instead of adding an Output operator).
     * Only operators that produce Texture output can be chain outputs. GeometryOperators
     * must be processed through a Render3D before output.
     *
     * Every vivid project should have exactly one texture output. Calling output()
     * multiple times will log a warning (only the last call takes effect).
     *
     * @par Example
     * @code
     * chain.add<Noise>("noise").scale(4.0f);
     * chain.add<HSV>("color").input("noise");
     * chain.output("color");  // Display the color operator
     * @endcode
     */
    void output(const std::string& name) {
        // Warn if output is being changed (indicates potential user error)
        if (outputWasSet_ && !outputName_.empty() && outputName_ != name) {
            std::cerr << "[Chain Warning] Output changed from '" << outputName_
                      << "' to '" << name << "'. Only one output is allowed per project." << std::endl;
        }
        outputWasSet_ = true;

        Operator* op = getByName(name);
        if (op && op->outputKind() != OutputKind::Texture) {
            error_ = "Output operator must produce a texture. '" + name + "' produces " +
                     outputKindName(op->outputKind()) + ". Route through Render3D first.";
            return;
        }
        outputName_ = name;
    }

    /**
     * @brief Get the designated output operator
     * @return Pointer to output operator, or nullptr if not set
     */
    Operator* getOutput() const;

    /**
     * @brief Get the output texture from the designated output operator
     * @return WebGPU texture, or nullptr if no output or not a texture operator
     */
    WGPUTexture outputTexture() const {
        Operator* out = getOutput();
        return out ? out->outputTexture() : nullptr;
    }

    // -------------------------------------------------------------------------
    /// @name Audio Output
    /// @{

    /**
     * @brief Specify which operator provides the audio output
     * @param name Name of the audio output operator
     *
     * Only operators that produce Audio output can be chain audio outputs.
     * The designated audio operator's output will be:
     * - Played through speakers (via AudioOutput)
     * - Captured for video export (via VideoExporter)
     *
     * @par Example
     * @code
     * chain.add<VideoAudio>("videoAudio").source("video");
     * chain.add<AudioOutput>("audioOut").input("videoAudio");
     * chain.audioOutput("audioOut");  // Route audio to speakers + export
     * @endcode
     */
    void audioOutput(const std::string& name);

    /**
     * @brief Get the designated audio output operator
     * @return Pointer to audio output operator, or nullptr if not set
     */
    Operator* getAudioOutput() const;

    /**
     * @brief Get the audio buffer from the designated audio output
     * @return Audio buffer, or nullptr if no audio output
     *
     * WARNING: For live playback only. For recording, use generateAudioForExport().
     */
    const AudioBuffer* audioOutputBuffer() const;

    /**
     * @brief Generate audio synchronously for video export
     * @param output Buffer to fill with audio samples (interleaved stereo)
     * @param frameCount Number of frames to generate
     *
     * Call this from the main thread during recording. Generates audio
     * deterministically in sync with video frames, avoiding race conditions.
     */
    void generateAudioForExport(float* output, uint32_t frameCount);

    /// @}
    // -------------------------------------------------------------------------

    /**
     * @brief Initialize all operators
     * @param ctx Runtime context
     *
     * Called automatically on first process(). Can be called explicitly
     * if you need operators initialized before the first frame.
     */
    void init(Context& ctx);

    /**
     * @brief Process all operators in dependency order
     * @param ctx Runtime context
     *
     * Automatically initializes operators on first call, computes
     * execution order, and processes each operator.
     *
     * Audio operators are processed multiple times per frame based on
     * elapsed time to maintain consistent audio sample rate.
     */
    void process(Context& ctx);

    // -------------------------------------------------------------------------
    /// @name State Preservation
    /// @{

    /**
     * @brief Save states from all operators
     * @return Map of operator names to state objects
     */
    std::map<std::string, std::unique_ptr<OperatorState>> saveAllStates();

    /**
     * @brief Restore states to matching operators
     * @param states Map of operator names to state objects
     */
    void restoreAllStates(std::map<std::string, std::unique_ptr<OperatorState>>& states);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Error Handling
    /// @{

    /// @brief Check if an error has occurred
    bool hasError() const { return !error_.empty(); }

    /// @brief Get the error message
    const std::string& error() const { return error_; }

    /// @brief Clear the error state
    void clearError() { error_.clear(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Introspection
    /// @{

    /**
     * @brief Get all operator names in add order
     * @return Vector of operator names
     */
    const std::vector<std::string>& operatorNames() const { return orderedNames_; }

    /**
     * @brief Get the audio graph for pull-based audio processing
     * @return Pointer to AudioGraph
     */
    AudioGraph* audioGraph() { return &audioGraph_; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Resolution Configuration
    /// @{

    /**
     * @brief Set window size (requested from runtime)
     * @param w Width in pixels
     * @param h Height in pixels
     * @return Reference for chaining
     *
     * Requests the runtime to resize the window. The request is honored
     * after chain initialization.
     *
     * @par Example
     * @code
     * void setup(Context& ctx) {
     *     auto& chain = ctx.chain();
     *     chain.windowSize(1920, 1080);
     * }
     * @endcode
     */
    Chain& windowSize(int w, int h) {
        m_windowWidth = w;
        m_windowHeight = h;
        m_windowSizeSet = true;
        return *this;
    }

    /// @brief Get requested window width
    int windowWidth() const { return m_windowWidth; }

    /// @brief Get requested window height
    int windowHeight() const { return m_windowHeight; }

    /// @brief Check if window size was requested
    bool hasWindowSize() const { return m_windowSizeSet; }

    /**
     * @brief Set default render resolution for generators
     * @param w Width in pixels
     * @param h Height in pixels
     * @return Reference for chaining
     *
     * Sets the default resolution that generators (Noise, Gradient, etc.) will use.
     * Individual operators can override with their own resolution() call.
     *
     * @par Example
     * @code
     * void setup(Context& ctx) {
     *     auto& chain = ctx.chain();
     *     chain.resolution(3840, 2160);  // 4K render
     *     chain.add<Noise>("noise");     // Renders at 3840x2160
     * }
     * @endcode
     */
    Chain& resolution(int w, int h) {
        m_defaultWidth = w;
        m_defaultHeight = h;
        m_resolutionSet = true;
        return *this;
    }

    /// @brief Get default render width
    int defaultWidth() const { return m_defaultWidth; }

    /// @brief Get default render height
    int defaultHeight() const { return m_defaultHeight; }

    /// @brief Check if default resolution was set
    bool hasResolution() const { return m_resolutionSet; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Debug Mode
    /// @{

    /**
     * @brief Enable debug logging for the chain
     * @param enabled Whether to enable debug logging
     *
     * When enabled, logs each operator's output target during process():
     * - Operator name and type
     * - Output texture dimensions
     * - Whether it's the final screen output
     *
     * Can also be enabled via VIVID_DEBUG_CHAIN=1 environment variable.
     */
    void setDebug(bool enabled) { debug_ = enabled; }

    /// @brief Check if debug mode is enabled
    bool isDebug() const { return debug_; }

    /**
     * @brief Print the output path from a given operator to screen
     * @param startName Name of operator to start from (empty = designated output)
     *
     * Useful for debugging render flow: which operator is actually
     * rendering to screen?
     */
    void debugOutputPath(const std::string& startName = "");

    /// @}

private:
    void computeExecutionOrder();
    void buildDependencyGraph();
    bool detectCycle();
    void checkDebugEnvVar();

    std::unordered_map<std::string, std::unique_ptr<Operator>> operators_;
    std::unordered_map<Operator*, std::string> operatorNames_;
    std::vector<std::string> orderedNames_;
    std::vector<Operator*> executionOrder_;
    std::vector<Operator*> visualExecutionOrder_;  // Non-audio operators only
    std::string outputName_;
    std::string audioOutputName_;
    std::string error_;
    bool needsSort_ = true;
    bool initialized_ = false;
    bool outputWasSet_ = false;  // Track if output() was called (for multi-call warning)

    // Pull-based audio graph (processed on audio thread)
    AudioGraph audioGraph_;
    AudioOutput* audioOutput_ = nullptr;

    // Legacy audio timing (for recording mode)
    double lastAudioTime_ = 0.0;
    double audioSamplesOwed_ = 0.0;

    // Debug mode
    bool debug_ = false;
    bool debugEnvChecked_ = false;

    // Resolution configuration
    int m_windowWidth = 0;
    int m_windowHeight = 0;
    bool m_windowSizeSet = false;

    int m_defaultWidth = 1280;
    int m_defaultHeight = 720;
    bool m_resolutionSet = false;
};

} // namespace vivid
