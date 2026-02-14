#pragma once

/**
 * @file operator.h
 * @brief Base class for all operators (effects, generators, etc.)
 *
 * Operators are the building blocks of vivid chains. Each operator
 * processes data and produces an output (typically a texture).
 */

#include <vivid/operator_viz.h>
#include <vivid/inspect_data.h>
#include <vivid/io/image_loader.h>
#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>

// Visualization drawing (ImGui-free)
#include <vivid/viz_draw_list.h>

namespace vivid {

class Context;

/**
 * @brief Output type classification for operators
 */
enum class OutputKind {
    Texture,    ///< GPU texture output (most common)
    CpuPixels,  ///< CPU pixel buffer (accessed via cpuPixelView())
    Value,      ///< Single float value
    ValueArray, ///< Array of float values
    Geometry,   ///< 3D geometry (meshes, scenes)
    Camera,     ///< Camera configuration
    Light,      ///< Light source
    Audio,      ///< Audio buffer output (PCM samples)
    AudioValue, ///< Audio analysis values (levels, FFT bands)
    Event       ///< Event stream (keyboard, mouse, timing, etc.)
};

/**
 * @brief Convert OutputKind to human-readable string
 * @param kind The output kind
 * @return String name of the output kind
 */
inline const char* outputKindName(OutputKind kind) {
    switch (kind) {
        case OutputKind::Texture:    return "Texture";
        case OutputKind::CpuPixels:  return "CpuPixels";
        case OutputKind::Value:      return "Value";
        case OutputKind::ValueArray: return "ValueArray";
        case OutputKind::Geometry:   return "Geometry";
        case OutputKind::Camera:     return "Camera";
        case OutputKind::Light:      return "Light";
        case OutputKind::Audio:      return "Audio";
        case OutputKind::AudioValue: return "AudioValue";
        case OutputKind::Event:      return "Event";
        default:                     return "Unknown";
    }
}

/**
 * @brief Parameter types for UI/serialization
 */
enum class ParamType {
    Float,      ///< Single float value
    Int,        ///< Integer value
    Bool,       ///< Boolean toggle
    Vec2,       ///< 2D vector (x, y)
    Vec3,       ///< 3D vector (x, y, z)
    Vec4,       ///< 4D vector (x, y, z, w)
    Color,      ///< RGBA color (0-1 range)
    String,     ///< Text string
    FilePath,   ///< File path (texture, video, model, etc.)
    Enum,       ///< Enumeration (dropdown selection)
    ADSR,       ///< ADSR envelope (attack, decay, sustain, release)
    DeviceList  ///< Dynamic device list dropdown (audio devices, etc.)
};

// Forward declaration for binding tracking
class Operator;

/**
 * @brief Parameter declaration for introspection and UI generation
 *
 * Contains metadata about a parameter including its name, type, and valid range.
 */
struct ParamDecl {
    std::string name;           ///< Display name
    ParamType type;             ///< Data type
    float minVal = 0.0f;        ///< Minimum value
    float maxVal = 1.0f;        ///< Maximum value
    float defaultVal[4] = {0, 0, 0, 0}; ///< Default value(s)

    // For String/FilePath parameters
    std::string stringDefault;  ///< Default string value
    std::string fileFilter;     ///< File filter (e.g., "*.png;*.jpg;*.exr")
    std::string fileCategory;   ///< Category hint ("image", "video", "audio", "model")

    // For Enum parameters
    std::vector<std::string> enumLabels;  ///< Display labels for enum values

    // For DeviceList parameters (dynamic list that can change at runtime)
    std::function<std::vector<std::string>()> deviceListProvider;  ///< Callback to get current device list

    // For trackable value operator bindings
    Operator* boundOperator = nullptr;  ///< Source value operator if bound (for visualization)
};

/**
 * @brief Base class for state preservation during hot-reload
 *
 * Operators can override saveState() and loadState() to preserve
 * internal state (like feedback buffers) across hot-reloads.
 */
struct OperatorState {
    virtual ~OperatorState() = default;
};

/**
 * @brief Texture state for preserving pixel data across hot-reload
 */
struct TextureState : public OperatorState {
    std::vector<uint8_t> pixels; ///< Raw pixel data
    int width = 0;               ///< Texture width
    int height = 0;              ///< Texture height

    /// @brief Check if state contains valid data
    bool hasData() const { return !pixels.empty() && width > 0 && height > 0; }
};

/**
 * @brief Abstract base class for all operators
 *
 * Operators follow a simple lifecycle:
 * 1. init() - Called once when the chain initializes
 * 2. process() - Called every frame to produce output
 * 3. cleanup() - Called when the operator is destroyed
 *
 * ## Demand-Based Cooking
 *
 * Operators use a generation-based system for efficient processing:
 * - Each operator has a `generation()` counter that increments when output changes
 * - `needsCook()` checks if inputs changed OR operator is dirty
 * - Call `markDirty()` in setters when parameters change
 * - Call `didCook()` at the end of process() to update generation
 *
 * ## Thread Safety
 *
 * Operators are NOT thread-safe. All methods (init, process, cleanup) are
 * called from the main render thread. Raw pointer members (m_inputs, etc.)
 * assume single-threaded access. For audio processing, use AudioOperator
 * which separates main-thread and audio-thread APIs.
 *
 * @par Example
 * @code
 * class MyEffect : public Operator {
 * public:
 *     void setAmount(float v) {
 *         if (m_amount != v) { m_amount = v; markDirty(); }
 *     }
 *
 *     void process(Context& ctx) override {
 *         if (!needsCook()) return;  // Skip if nothing changed
 *         // ... do work ...
 *         didCook();  // Mark output as updated
 *     }
 * };
 * @endcode
 */
class Operator {
public:
    virtual ~Operator() = default;

    // -------------------------------------------------------------------------
    /// @name Lifecycle
    /// @{

    /**
     * @brief Initialize the operator
     * @param ctx Runtime context with GPU device, queue, etc.
     *
     * Called once when the chain initializes. Create GPU resources here.
     */
    virtual void init(Context& ctx) {}

    /**
     * @brief Process one frame
     * @param ctx Runtime context with time, input, etc.
     *
     * Called every frame. Read inputs, compute output.
     *
     * Note: Operators automatically register themselves for visualization
     * the first time process() is called. Set autoRegisterName before
     * calling process() to specify a custom registration name.
     */
    virtual void process(Context& ctx) = 0;

    /**
     * @brief Process with automatic registration
     * @param ctx Runtime context
     * @param registerName Name to use for auto-registration
     *
     * Calls process() and ensures operator is registered for visualization.
     */
    void processAndRegister(Context& ctx, const std::string& registerName);

    /**
     * @brief Clean up resources
     *
     * Called when the operator is destroyed. Release GPU resources here.
     */
    virtual void cleanup() {}

    /**
     * @brief Check if operator has been initialized
     * @return True if init() has completed successfully
     */
    [[nodiscard]] bool isInitialized() const { return m_initialized; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Metadata
    /// @{

    /**
     * @brief Get the operator's display name
     * @return Human-readable name (e.g., "Noise", "Blur")
     */
    [[nodiscard]] virtual std::string name() const = 0;

    /**
     * @brief Get the output type
     * @return OutputKind indicating what this operator produces
     */
    [[nodiscard]] virtual OutputKind outputKind() const { return OutputKind::Texture; }

    /**
     * @brief Get parameter declarations for UI/introspection
     * @return Vector of ParamDecl describing all parameters
     *
     * Override to expose parameters for external control (OSC, GUI).
     */
    virtual std::vector<ParamDecl> params() { return {}; }

    /**
     * @brief Get current parameter value
     * @param name Parameter name
     * @param out Array to receive value (up to 4 floats)
     * @return True if parameter exists
     *
     * Override to allow reading parameter values.
     */
    virtual bool getParam(const std::string& name, float out[4]) { return false; }

    /**
     * @brief Set parameter value
     * @param name Parameter name
     * @param value Array of values (1-4 floats depending on type)
     * @return True if parameter was set successfully
     *
     * Override to allow setting parameter values.
     */
    virtual bool setParam(const std::string& name, const float value[4]) { return false; }

    /**
     * @brief Get introspection data for this operator
     * @return InspectData with metrics and metadata
     *
     * Default implementation reports enabled state and all params.
     * Override to add domain-specific computed metrics (energy, rms, etc.).
     */
    virtual InspectData inspect() const {
        InspectData data;
        data.set("enabled", m_bypassed ? 0.0f : 1.0f);
        // Auto-populate from params()
        auto paramDecls = const_cast<Operator*>(this)->params();
        for (const auto& p : paramDecls) {
            float val[4] = {0};
            if (const_cast<Operator*>(this)->getParam(p.name, val)) {
                data.set(p.name, val[0]);
            }
        }
        return data;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Output
    /// @{

    /**
     * @brief Get the output texture view
     * @return WebGPU texture view for visualization/chaining
     */
    [[nodiscard]] virtual WGPUTextureView outputView() const { return nullptr; }

    /**
     * @brief Get the raw output texture (for video export/capture)
     * @return WebGPU texture, or nullptr if not a texture operator
     */
    [[nodiscard]] virtual WGPUTexture outputTexture() const { return nullptr; }

    /**
     * @brief Get effective output (follows bypass chain)
     * @return Output view, or first input's output if bypassed
     *
     * Use this when you need to respect bypass state. If this operator
     * is bypassed, returns the first input's effective output instead.
     */
    [[nodiscard]] WGPUTextureView effectiveOutputView() const {
        if (m_bypassed && !m_inputs.empty() && m_inputs[0]) {
            return m_inputs[0]->effectiveOutputView();
        }
        return outputView();
    }

    /**
     * @brief Get the output value (for Value/ValueArray operators)
     * @return The current output value, or 0.0 if not a value operator
     */
    [[nodiscard]] virtual float outputValue() const { return 0.0f; }

    /**
     * @brief Get CPU pixel data if available
     * @return ImageData with pixel buffer, or nullopt if not available
     *
     * Override in operators that maintain CPU pixel buffers (e.g., Webcam).
     * Useful for ML inference which needs CPU access without GPU readback.
     */
    [[nodiscard]] virtual std::optional<io::ImageData> cpuPixels() const { return std::nullopt; }

    /**
     * @brief Zero-copy CPU pixel pointer access
     */
    struct CpuPixelView {
        const uint8_t* data = nullptr;
        int width = 0;
        int height = 0;
        int channels = 4;
        size_t stride = 0;  ///< Bytes per row (0 = width * channels)

        [[nodiscard]] bool valid() const { return data && width > 0 && height > 0; }
        [[nodiscard]] size_t rowStride() const { return stride ? stride : width * channels; }
    };

    /**
     * @brief Get zero-copy pointer to CPU pixel data
     * @return CpuPixelView with pointer to pixel data, or invalid view if not available
     *
     * Faster than cpuPixels() as it avoids copying. The pointer is valid until
     * the next frame. Override in operators with CPU pixel buffers.
     */
    [[nodiscard]] virtual CpuPixelView cpuPixelView() const { return {}; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Preservation
    /// @{

    /**
     * @brief Save state for hot-reload
     * @return Unique pointer to state object, or nullptr if no state
     *
     * Override to preserve internal state (e.g., feedback buffers) across hot-reloads.
     */
    virtual std::unique_ptr<OperatorState> saveState() { return nullptr; }

    /**
     * @brief Restore state after hot-reload
     * @param state Previously saved state object
     */
    virtual void loadState(std::unique_ptr<OperatorState> state) {}

    /// @}
    // -------------------------------------------------------------------------
    /// @name Input Connections
    /// @{

    /**
     * @brief Add an input connection
     * @param op Operator to connect as input
     */
    void setInput(Operator* op) { m_inputs.push_back(op); }

    /**
     * @brief Set input at specific index
     * @param index Input slot index
     * @param op Operator to connect
     */
    void setInput(int index, Operator* op) {
        if (index >= static_cast<int>(m_inputs.size())) {
            m_inputs.resize(index + 1, nullptr);
        }
        m_inputs[index] = op;
    }

    /**
     * @brief Get input operator
     * @param index Input slot index (default 0)
     * @return Connected operator, or nullptr if none
     */
    [[nodiscard]] Operator* getInput(int index = 0) const {
        return (index < static_cast<int>(m_inputs.size())) ? m_inputs[index] : nullptr;
    }

    /**
     * @brief Get number of connected inputs
     * @return Input count
     */
    [[nodiscard]] size_t inputCount() const { return m_inputs.size(); }

    /**
     * @brief Set input by name (resolved at init time)
     * @param index Input slot index
     * @param name Name of operator to connect
     */
    void setInputByName(int index, const std::string& name) {
        if (index >= static_cast<int>(m_inputNames.size())) {
            m_inputNames.resize(index + 1);
        }
        m_inputNames[index] = name;
    }

    /**
     * @brief Get input pin label at index
     * @param index Input slot index
     * @return Label for the input pin (for node graph visualization)
     *
     * Override in derived classes to provide meaningful pin names.
     * Default returns empty string (visualizer will use "in0", "in1", etc.)
     */
    [[nodiscard]] virtual std::string getInputName(int index) const {
        return (index < static_cast<int>(m_inputNames.size()))
            ? m_inputNames[index] : std::string();
    }

    /**
     * @brief Get number of named inputs
     * @return Count of input names
     */
    [[nodiscard]] size_t inputNameCount() const { return m_inputNames.size(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Bypass
    /// @{

    /**
     * @brief Set bypass state
     * @param bypassed If true, operator passes through its first input unchanged
     *
     * When bypassed, process() is skipped and outputView() returns the first
     * input's output instead. Useful for A/B testing effects.
     */
    void setBypassed(bool bypassed) { m_bypassed = bypassed; }

    /**
     * @brief Check if operator is bypassed
     * @return True if bypassed
     */
    [[nodiscard]] bool isBypassed() const { return m_bypassed; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Trigger Source (for audio-thread timing)
    /// @{

    /**
     * @brief Set trigger source operator
     * @param source Operator that triggers this one
     *
     * For AudioOperators, this enables automatic audio-thread triggering.
     * When the source operator's triggered() returns true, this operator's
     * onTrigger() will be called automatically during generateBlock().
     *
     * Also displayed in chain visualizer as cyan dashed lines.
     */
    void setTriggerSource(Operator* source) { m_triggerSource = source; }

    /**
     * @brief Set trigger source by name (resolved at init time)
     * @param name Name of the trigger source operator
     *
     * @par Example
     * @code
     * auto& seq = chain.add<Sequencer>("seq");
     * seq.setTriggerSource("clock");  // Advance on clock trigger
     *
     * auto& kick = chain.add<Kick>("kick");
     * kick.setTriggerSource("seq");  // Trigger on sequencer output
     * @endcode
     */
    void setTriggerSource(const std::string& name) { m_pendingTriggerSourceName = name; }

    /**
     * @brief Get trigger source operator
     * @return Trigger source, or nullptr if none
     */
    Operator* triggerSource() const { return m_triggerSource; }

    /**
     * @brief Get pending trigger source name (for deferred resolution)
     * @return Name string, or empty if already resolved
     */
    const std::string& pendingTriggerSourceName() const { return m_pendingTriggerSourceName; }

    /**
     * @brief Clear pending trigger source name after resolution
     */
    void clearPendingTriggerSourceName() { m_pendingTriggerSourceName.clear(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Event Source (for visualization)
    /// @{

    /**
     * @brief Set event source operator for visualization
     * @param source Operator that sends events to this one
     *
     * Similar to setTriggerSource(), this is for chain visualizer display.
     * Shows green dashed lines from event sources (e.g., KeyboardIn -> Player).
     * The actual event handling must still be done in update() code.
     */
    void setEventSource(Operator* source) { m_eventSource = source; }

    /**
     * @brief Set event source by name (resolved at init time)
     * @param name Name of the event source operator
     */
    void setEventSource(const std::string& name) { m_pendingEventSourceName = name; }

    /**
     * @brief Get event source operator
     * @return Event source, or nullptr if none
     */
    Operator* eventSource() const { return m_eventSource; }

    /**
     * @brief Get pending event source name (for deferred resolution)
     * @return Name string, or empty if already resolved
     */
    const std::string& pendingEventSourceName() const { return m_pendingEventSourceName; }

    /**
     * @brief Clear pending event source name after resolution
     */
    void clearPendingEventSourceName() { m_pendingEventSourceName.clear(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Trigger State (for audio-thread timing)
    /// @{

    /**
     * @brief Check if this operator triggered in the current audio block
     * @return True if operator triggered, false otherwise
     *
     * Override in operators that can act as trigger sources (Sequencer, Euclidean).
     * This allows downstream operators to check trigger state on the audio thread.
     */
    virtual bool triggered() const { return false; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Cooking / Dependency System
    /// @{

    /**
     * @brief Check if operator needs to cook (process)
     * @return True if any input changed or operator is self-dirty
     *
     * Call this at the start of process() to skip unnecessary work.
     * Compares current input generations to cached values.
     */
    [[nodiscard]] bool needsCook() const {
        // Always cook if marked dirty
        if (m_selfDirty) return true;

        // Check if any input generation changed
        for (size_t i = 0; i < m_inputs.size(); ++i) {
            if (!m_inputs[i]) continue;

            uint64_t inputGen = m_inputs[i]->generation();
            if (i >= m_cachedInputGens.size() || m_cachedInputGens[i] != inputGen) {
                return true;
            }
        }

        return false;
    }

    /**
     * @brief Mark operator as dirty (needs recook)
     *
     * Call this in setters when parameters change.
     */
    void markDirty() { m_selfDirty = true; }

    /**
     * @brief Called after process() completes
     *
     * Clears dirty flag and caches current input generations.
     * Increments generation counter to notify downstream operators.
     */
    void didCook() {
        m_selfDirty = false;
        m_generation++;

        // Cache current input generations
        m_cachedInputGens.resize(m_inputs.size());
        for (size_t i = 0; i < m_inputs.size(); ++i) {
            m_cachedInputGens[i] = m_inputs[i] ? m_inputs[i]->generation() : 0;
        }
    }

    /**
     * @brief Get current output generation
     * @return Generation counter (increments each time output changes)
     *
     * Downstream operators use this to detect when inputs changed.
     */
    [[nodiscard]] uint64_t generation() const { return m_generation; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Visualization
    /// @{

    /**
     * @brief Draw custom visualization in the chain visualizer
     * @param drawList ImGui draw list for rendering
     * @param minX Left edge of drawing area
     * @param minY Top edge of drawing area
     * @param maxX Right edge of drawing area
     * @param maxY Bottom edge of drawing area
     * @return true if custom visualization was drawn, false to use default
     *
     * Override to draw custom visualizations for your operator.
     * The chain visualizer calls this instead of rendering the default
     * waveform/texture preview when it returns true.
     *
     * @code
     * bool drawVisualization(VizDrawList* dl, float minX, float minY,
     *                        float maxX, float maxY) override {
     *     // Draw envelope shape
     *     dl->AddRectFilled({minX, minY}, {maxX, maxY}, VIZ_COL32(0, 0, 0, 255));
     *     return true;
     * }
     * @endcode
     */
    virtual bool drawVisualization(VizDrawList* drawList,
                                   float minX, float minY,
                                   float maxX, float maxY) { return false; }

    /**
     * @brief Get visualization data for chain visualizer
     * @return OperatorVizData struct with visualization hints
     *
     * Alternative to drawVisualization() - return data and let
     * the visualizer render it. Useful for simple visualizations.
     */
    virtual OperatorVizData getVisualizationData() const { return {}; }

    /// @}

    int sourceLine = 0; ///< Source line number (for editor integration)

    /// @brief Name used for auto-registration (set before process if needed)
    std::string autoRegisterName;

protected:
    /**
     * @brief Guard for double-initialization
     * @return True if init should proceed, false if already initialized
     *
     * Call at the start of init() to prevent double-initialization:
     * @code
     * void init(Context& ctx) override {
     *     if (!beginInit()) return;
     *     // ... setup code ...
     * }
     * @endcode
     */
    bool beginInit() {
        if (m_initialized) return false;
        m_initialized = true;
        return true;
    }

    /**
     * @brief Reset initialization state (for hot-reload)
     *
     * Call this when an operator needs to be re-initialized,
     * typically during hot-reload when resources need to be recreated.
     */
    void resetInit() { m_initialized = false; }

    std::vector<Operator*> m_inputs;      ///< Connected input operators (resolved pointers)
    std::vector<std::string> m_inputNames; ///< Input names for deferred resolution
    bool m_registered = false;            ///< Whether already registered for visualization
    bool m_bypassed = false;         ///< Whether operator is bypassed (pass-through)
    bool m_initialized = false;      ///< Whether init() has completed

    // Trigger source (for chain visualizer - shows trigger flow)
    Operator* m_triggerSource = nullptr;           ///< Trigger source operator (for visualization)
    std::string m_pendingTriggerSourceName;        ///< Deferred trigger source name

    // Event source (for chain visualizer - shows event flow)
    Operator* m_eventSource = nullptr;             ///< Event source operator (for visualization)
    std::string m_pendingEventSourceName;          ///< Deferred event source name

    // Cooking system
    uint64_t m_generation = 0;                ///< Output generation counter
    mutable std::vector<uint64_t> m_cachedInputGens; ///< Cached input generations from last cook
    mutable bool m_selfDirty = true;          ///< True if parameters changed (starts dirty)
};

} // namespace vivid
