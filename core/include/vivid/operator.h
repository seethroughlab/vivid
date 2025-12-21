#pragma once

/**
 * @file operator.h
 * @brief Base class for all operators (effects, generators, etc.)
 *
 * Operators are the building blocks of vivid chains. Each operator
 * processes data and produces an output (typically a texture).
 */

#include <vivid/operator_viz.h>
#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <memory>

// Forward declaration for ImGui drawing
struct ImDrawList;

namespace vivid {

class Context;

/**
 * @brief Output type classification for operators
 */
enum class OutputKind {
    Texture,    ///< GPU texture output (most common)
    Value,      ///< Single float value
    ValueArray, ///< Array of float values
    Geometry,   ///< 3D geometry (meshes, scenes)
    Camera,     ///< Camera configuration
    Light,      ///< Light source
    Audio,      ///< Audio buffer output (PCM samples)
    AudioValue  ///< Audio analysis values (levels, FFT bands)
};

/**
 * @brief Convert OutputKind to human-readable string
 * @param kind The output kind
 * @return String name of the output kind
 */
inline const char* outputKindName(OutputKind kind) {
    switch (kind) {
        case OutputKind::Texture:    return "Texture";
        case OutputKind::Value:      return "Value";
        case OutputKind::ValueArray: return "ValueArray";
        case OutputKind::Geometry:   return "Geometry";
        case OutputKind::Camera:     return "Camera";
        case OutputKind::Light:      return "Light";
        case OutputKind::Audio:      return "Audio";
        case OutputKind::AudioValue: return "AudioValue";
        default:                     return "Unknown";
    }
}

/**
 * @brief Parameter types for UI/serialization
 */
enum class ParamType {
    Float,    ///< Single float value
    Int,      ///< Integer value
    Bool,     ///< Boolean toggle
    Vec2,     ///< 2D vector (x, y)
    Vec3,     ///< 3D vector (x, y, z)
    Vec4,     ///< 4D vector (x, y, z, w)
    Color,    ///< RGBA color (0-1 range)
    String,   ///< Text string
    FilePath  ///< File path (texture, video, model, etc.)
};

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
    bool isInitialized() const { return m_initialized; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Metadata
    /// @{

    /**
     * @brief Get the operator's display name
     * @return Human-readable name (e.g., "Noise", "Blur")
     */
    virtual std::string name() const = 0;

    /**
     * @brief Get the output type
     * @return OutputKind indicating what this operator produces
     */
    virtual OutputKind outputKind() const { return OutputKind::Texture; }

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

    /// @}
    // -------------------------------------------------------------------------
    /// @name Output
    /// @{

    /**
     * @brief Get the output texture view
     * @return WebGPU texture view for visualization/chaining
     */
    virtual WGPUTextureView outputView() const { return nullptr; }

    /**
     * @brief Get the raw output texture (for video export/capture)
     * @return WebGPU texture, or nullptr if not a texture operator
     */
    virtual WGPUTexture outputTexture() const { return nullptr; }

    /**
     * @brief Get effective output (follows bypass chain)
     * @return Output view, or first input's output if bypassed
     *
     * Use this when you need to respect bypass state. If this operator
     * is bypassed, returns the first input's effective output instead.
     */
    WGPUTextureView effectiveOutputView() const {
        if (m_bypassed && !m_inputs.empty() && m_inputs[0]) {
            return m_inputs[0]->effectiveOutputView();
        }
        return outputView();
    }

    /**
     * @brief Get the output value (for Value/ValueArray operators)
     * @return The current output value, or 0.0 if not a value operator
     */
    virtual float outputValue() const { return 0.0f; }

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
    Operator* getInput(int index = 0) const {
        return (index < static_cast<int>(m_inputs.size())) ? m_inputs[index] : nullptr;
    }

    /**
     * @brief Get number of connected inputs
     * @return Input count
     */
    size_t inputCount() const { return m_inputs.size(); }

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
     * @brief Get input name at index
     * @param index Input slot index
     * @return Name of connected operator, or empty string if none
     */
    const std::string& getInputName(int index) const {
        static const std::string empty;
        return (index < static_cast<int>(m_inputNames.size()))
            ? m_inputNames[index] : empty;
    }

    /**
     * @brief Get number of named inputs
     * @return Count of input names
     */
    size_t inputNameCount() const { return m_inputNames.size(); }

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
    bool isBypassed() const { return m_bypassed; }

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
    bool needsCook() const {
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
    uint64_t generation() const { return m_generation; }

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
     * bool drawVisualization(ImDrawList* dl, float minX, float minY,
     *                        float maxX, float maxY) override {
     *     // Draw envelope shape
     *     dl->AddRectFilled({minX, minY}, {maxX, maxY}, 0xFF000000);
     *     return true;
     * }
     * @endcode
     */
    virtual bool drawVisualization(ImDrawList* drawList,
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

    // Cooking system
    uint64_t m_generation = 0;                ///< Output generation counter
    mutable std::vector<uint64_t> m_cachedInputGens; ///< Cached input generations from last cook
    mutable bool m_selfDirty = true;          ///< True if parameters changed (starts dirty)
};

} // namespace vivid
