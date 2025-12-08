#pragma once

/**
 * @file operator.h
 * @brief Base class for all operators (effects, generators, etc.)
 *
 * Operators are the building blocks of vivid chains. Each operator
 * processes data and produces an output (typically a texture).
 */

#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <memory>

namespace vivid {

class Context;

/**
 * @brief Output type classification for operators
 */
enum class OutputKind {
    Texture,    ///< GPU texture output (most common)
    Value,      ///< Single float value
    ValueArray, ///< Array of float values
    Geometry    ///< 3D geometry
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
        default:                     return "Unknown";
    }
}

/**
 * @brief Parameter types for UI/serialization
 */
enum class ParamType {
    Float,  ///< Single float value
    Int,    ///< Integer value
    Bool,   ///< Boolean toggle
    Vec2,   ///< 2D vector (x, y)
    Vec3,   ///< 3D vector (x, y, z)
    Vec4,   ///< 4D vector (x, y, z, w)
    Color,  ///< RGBA color (0-1 range)
    String  ///< Text string
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
 * @par Example
 * @code
 * class MyEffect : public Operator {
 * public:
 *     void init(Context& ctx) override { ... }
 *     void process(Context& ctx) override { ... }
 *     std::string name() const override { return "MyEffect"; }
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
     */
    virtual std::vector<ParamDecl> params() { return {}; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Output
    /// @{

    /**
     * @brief Get the output texture view
     * @return WebGPU texture view for visualization/chaining
     */
    virtual WGPUTextureView outputView() const { return nullptr; }

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
    void setInput(Operator* op) { inputs_.push_back(op); }

    /**
     * @brief Set input at specific index
     * @param index Input slot index
     * @param op Operator to connect
     */
    void setInput(int index, Operator* op) {
        if (index >= static_cast<int>(inputs_.size())) {
            inputs_.resize(index + 1, nullptr);
        }
        inputs_[index] = op;
    }

    /**
     * @brief Get input operator
     * @param index Input slot index (default 0)
     * @return Connected operator, or nullptr if none
     */
    Operator* getInput(int index = 0) const {
        return (index < static_cast<int>(inputs_.size())) ? inputs_[index] : nullptr;
    }

    /**
     * @brief Get number of connected inputs
     * @return Input count
     */
    size_t inputCount() const { return inputs_.size(); }

    /// @}

    int sourceLine = 0; ///< Source line number (for editor integration)

    /// @brief Name used for auto-registration (set before process if needed)
    std::string autoRegisterName;

protected:
    std::vector<Operator*> inputs_; ///< Connected input operators
    bool m_registered = false;      ///< Whether already registered for visualization
};

} // namespace vivid
