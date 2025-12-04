#pragma once

#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

#include "GraphicsTypes.h"

// Forward declarations
namespace Diligent {
    struct ITexture;
    struct ITextureView;
    struct IPipelineState;
    struct IShaderResourceBinding;
    struct IBuffer;
    struct ISampler;
}

namespace vivid {

class Context;
class Operator;

/// Output type for operators
enum class OutputKind {
    Texture,    // 2D image
    Value,      // Single float
    ValueArray, // Float array
    Geometry    // 3D mesh (future)
};

/// Base class for state preservation across hot-reload
struct OperatorState {
    virtual ~OperatorState() = default;
};

/// Parameter types for editor integration
enum class ParamType {
    Float,
    Int,
    Bool,
    Vec2,
    Vec3,
    Vec4,
    Color,
    String
};

/// Parameter declaration for editor integration
struct ParamDecl {
    std::string name;
    ParamType type;
    float minVal = 0.0f;
    float maxVal = 1.0f;
    float defaultVal[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

// Helper functions to create parameter declarations
inline ParamDecl floatParam(const std::string& name, float defaultVal, float minVal = 0.0f, float maxVal = 1.0f) {
    ParamDecl p{name, ParamType::Float, minVal, maxVal, {defaultVal, 0, 0, 0}};
    return p;
}

inline ParamDecl intParam(const std::string& name, int defaultVal, int minVal = 0, int maxVal = 100) {
    ParamDecl p{name, ParamType::Int, static_cast<float>(minVal), static_cast<float>(maxVal),
                {static_cast<float>(defaultVal), 0, 0, 0}};
    return p;
}

inline ParamDecl boolParam(const std::string& name, bool defaultVal = false) {
    ParamDecl p{name, ParamType::Bool, 0, 1, {defaultVal ? 1.0f : 0.0f, 0, 0, 0}};
    return p;
}

inline ParamDecl colorParam(const std::string& name, float r = 1, float g = 1, float b = 1, float a = 1) {
    ParamDecl p{name, ParamType::Color, 0, 1, {r, g, b, a}};
    return p;
}

/// Base class for all operators
class Operator {
public:
    virtual ~Operator() = default;

    /// Called once when the operator is created
    virtual void init(Context& ctx) {}

    /// Called every frame to process
    virtual void process(Context& ctx) = 0;

    /// Called before the operator is destroyed
    virtual void cleanup() {}

    /// Return state to preserve across hot-reload
    virtual std::unique_ptr<OperatorState> saveState() { return nullptr; }

    /// Restore state after hot-reload
    virtual void loadState(std::unique_ptr<OperatorState> state) {}

    /// Return parameter declarations for editor
    virtual std::vector<ParamDecl> params() { return {}; }

    /// Return the output type
    virtual OutputKind outputKind() const { return OutputKind::Texture; }

    /// Return the operator name (for debugging/display)
    virtual std::string typeName() const = 0;

    // --- Input/Output ---

    /// Get the output texture (for operators that produce textures)
    virtual Diligent::ITextureView* getOutputSRV() { return nullptr; }

    /// Get the output render target view
    virtual Diligent::ITextureView* getOutputRTV() { return nullptr; }

    /// Set input operator (single input)
    void setInput(Operator* op) { inputs_.clear(); inputs_.push_back(op); }

    /// Set input at specific index
    void setInput(int index, Operator* op) {
        if (index >= static_cast<int>(inputs_.size())) {
            inputs_.resize(index + 1, nullptr);
        }
        inputs_[index] = op;
    }

    /// Get input operator
    Operator* getInput(int index = 0) const {
        return (index < static_cast<int>(inputs_.size())) ? inputs_[index] : nullptr;
    }

    /// Get input texture SRV
    Diligent::ITextureView* getInputSRV(int index = 0) const {
        Operator* in = getInput(index);
        return in ? in->getOutputSRV() : nullptr;
    }

    /// Source line number (for VS Code decorations)
    int sourceLine = 0;

    /// Instance name in the chain
    std::string instanceName;

protected:
    std::vector<Operator*> inputs_;
};

/// Base class for 2D texture effect operators
/// Provides common infrastructure for fullscreen shader effects
class TextureOperator : public Operator {
public:
    virtual ~TextureOperator();

    void init(Context& ctx) override;
    void cleanup() override;

    OutputKind outputKind() const override { return OutputKind::Texture; }

    Diligent::ITextureView* getOutputSRV() override;
    Diligent::ITextureView* getOutputRTV() override;

protected:
    /// Create the pipeline state - override in subclass
    virtual void createPipeline(Context& ctx) = 0;

    /// Update uniform buffer before rendering - override in subclass
    virtual void updateUniforms(Context& ctx) {}

    /// Render the effect using the pipeline
    void renderFullscreen(Context& ctx);

    /// Create a uniform buffer of the given size
    void createUniformBuffer(Context& ctx, size_t size);

    // Resources
    Diligent::ITexture* outputTexture_ = nullptr;
    Diligent::ITextureView* outputSRV_ = nullptr;
    Diligent::ITextureView* outputRTV_ = nullptr;
    Diligent::IPipelineState* pso_ = nullptr;
    Diligent::IShaderResourceBinding* srb_ = nullptr;
    Diligent::IBuffer* uniformBuffer_ = nullptr;

    int outputWidth_ = 0;
    int outputHeight_ = 0;
};

} // namespace vivid

// Macro to register an operator type
#define VIVID_OPERATOR(ClassName) \
    extern "C" vivid::Operator* create_##ClassName() { return new ClassName(); }
