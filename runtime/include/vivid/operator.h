#pragma once
#include "context.h"
#include "types.h"
#include <memory>
#include <vector>
#include <string>

namespace vivid {

/**
 * @brief Base class for serializable operator state.
 *
 * Derive from this to store state that should be preserved across hot-reload.
 * Return your derived state from Operator::saveState() and restore it in
 * Operator::loadState().
 *
 * @code
 * struct MyState : OperatorState {
 *     float phase = 0.0f;
 * };
 * @endcode
 */
struct OperatorState {
    virtual ~OperatorState() = default;
};

/**
 * @brief Base class for all Vivid operators.
 *
 * Operators are the building blocks of Vivid pipelines. Each operator takes
 * inputs (textures or values), processes them, and produces outputs.
 *
 * ## Lifecycle
 * - `init()`: Called once when the operator is created. Create textures here.
 * - `process()`: Called every frame. Generate your output here.
 * - `cleanup()`: Called before destruction. Release resources here.
 *
 * ## Hot Reload
 * Override `saveState()` and `loadState()` to preserve state across hot-reload.
 *
 * ## Example
 * @code
 * class MyOperator : public Operator {
 *     Texture output_;
 * public:
 *     void init(Context& ctx) override {
 *         output_ = ctx.createTexture();
 *     }
 *     void process(Context& ctx) override {
 *         ctx.runShader("shaders/my.wgsl", nullptr, output_);
 *         ctx.setOutput("out", output_);
 *     }
 *     OutputKind outputKind() override { return OutputKind::Texture; }
 * };
 * VIVID_OPERATOR(MyOperator)
 * @endcode
 */
class Operator {
public:
    virtual ~Operator() = default;

    /**
     * @brief Called once when the operator is first created.
     * @param ctx The context for accessing runtime services.
     *
     * Use this to create textures and initialize state. Not called on hot-reload.
     */
    virtual void init(Context& ctx) {}

    /**
     * @brief Called every frame to produce output.
     * @param ctx The context for accessing time, shaders, and other operators.
     *
     * This is the main processing function. Read inputs, run shaders, and
     * set outputs here.
     */
    virtual void process(Context& ctx) = 0;

    /**
     * @brief Called before the operator is destroyed.
     *
     * Use this to release any resources not managed by the runtime.
     * Textures created via Context are automatically cleaned up.
     */
    virtual void cleanup() {}

    /**
     * @brief Save state for hot-reload.
     * @return State object to preserve, or nullptr if no state to save.
     *
     * Called before the operator is destroyed during hot-reload. Return
     * a custom OperatorState subclass containing any state that should
     * survive the reload (e.g., animation phase, accumulated values).
     */
    virtual std::unique_ptr<OperatorState> saveState() { return nullptr; }

    /**
     * @brief Restore state after hot-reload.
     * @param state The state previously returned by saveState().
     *
     * Called after the operator is recreated during hot-reload. Cast
     * the state to your custom subclass and restore values.
     */
    virtual void loadState(std::unique_ptr<OperatorState> state) {}

    /**
     * @brief Declare parameters for the editor.
     * @return Vector of parameter declarations.
     *
     * Override this to expose tweakable parameters in the VS Code extension.
     * Use helper functions like floatParam(), intParam(), etc.
     */
    virtual std::vector<ParamDecl> params() { return {}; }

    /**
     * @brief Specify the type of output this operator produces.
     * @return The output kind (Texture, Value, ValueArray, or Geometry).
     *
     * This affects how the preview is displayed in VS Code.
     */
    virtual OutputKind outputKind() { return OutputKind::Texture; }

    /**
     * @brief Check if this operator needs to be processed this frame.
     * @param ctx The context for checking time, inputs, etc.
     * @return true if process() should be called, false to skip.
     *
     * Override this for lazy evaluation optimization. By default, operators
     * are always processed every frame. Return false to skip processing
     * when inputs haven't changed.
     *
     * @note This is an optimization hint. The runtime may still call process()
     * if needed for dependency resolution.
     */
    virtual bool needsUpdate(Context& ctx) { return true; }

    /// @brief Set the operator's unique identifier (called by VIVID_OPERATOR macro).
    void setId(const std::string& id) { id_ = id; }

    /// @brief Set the source line number (called by VIVID_OPERATOR macro).
    void setSourceLine(int line) { sourceLine_ = line; }

    /// @brief Get the operator's unique identifier.
    const std::string& id() const { return id_; }

    /// @brief Get the source line where this operator was defined.
    int sourceLine() const { return sourceLine_; }

protected:
    std::string id_;
    int sourceLine_ = 0;
};

} // namespace vivid

// Export macros for shared library operators
#if defined(_WIN32)
    #define VIVID_EXPORT extern "C" __declspec(dllexport)
#else
    #define VIVID_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Legacy single-operator macro (for backwards compatibility)
#define VIVID_OPERATOR(ClassName) \
    static const int vivid_source_line_ = __LINE__; \
    VIVID_EXPORT vivid::Operator* vivid_create_operator() { \
        auto* op = new ClassName(); \
        op->setId(#ClassName); \
        op->setSourceLine(vivid_source_line_); \
        return op; \
    } \
    VIVID_EXPORT void vivid_destroy_operator(vivid::Operator* op) { \
        delete op; \
    } \
    VIVID_EXPORT const char* vivid_operator_name() { \
        return #ClassName; \
    } \
    VIVID_EXPORT int vivid_operator_source_line() { \
        return vivid_source_line_; \
    }

// Chain API macros - use these for declarative operator chains
// User defines: void setup(Chain& chain) and optionally void update(Chain& chain, Context& ctx)

// Forward declarations for Chain API
namespace vivid {
    class Chain;
    class Context;
}

#define VIVID_CHAIN_SETUP(setupFunc) \
    VIVID_EXPORT void vivid_setup(vivid::Chain& chain) { \
        setupFunc(chain); \
    }

#define VIVID_CHAIN_UPDATE(updateFunc) \
    VIVID_EXPORT void vivid_update(vivid::Chain& chain, vivid::Context& ctx) { \
        updateFunc(chain, ctx); \
    }

// Convenience macro that exports both setup and update
#define VIVID_CHAIN(setupFunc, updateFunc) \
    VIVID_CHAIN_SETUP(setupFunc) \
    VIVID_CHAIN_UPDATE(updateFunc)

// Use this if you only need setup (no per-frame update)
#define VIVID_CHAIN_SETUP_ONLY(setupFunc) \
    VIVID_CHAIN_SETUP(setupFunc)
