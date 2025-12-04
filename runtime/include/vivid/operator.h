#pragma once

#include <string>
#include <vector>
#include <memory>

namespace vivid {

class Context;

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
    Color,
    String
};

/// Parameter declaration for editor integration
struct ParamDecl {
    std::string name;
    ParamType type;
    float minVal = 0.0f;
    float maxVal = 1.0f;
    // Default values stored as floats (up to 4 components for vectors)
    float defaultVal[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

// Helper functions to create parameter declarations
inline ParamDecl floatParam(const std::string& name, float defaultVal, float minVal = 0.0f, float maxVal = 1.0f) {
    ParamDecl p;
    p.name = name;
    p.type = ParamType::Float;
    p.minVal = minVal;
    p.maxVal = maxVal;
    p.defaultVal[0] = defaultVal;
    return p;
}

inline ParamDecl intParam(const std::string& name, int defaultVal, int minVal = 0, int maxVal = 100) {
    ParamDecl p;
    p.name = name;
    p.type = ParamType::Int;
    p.minVal = static_cast<float>(minVal);
    p.maxVal = static_cast<float>(maxVal);
    p.defaultVal[0] = static_cast<float>(defaultVal);
    return p;
}

inline ParamDecl boolParam(const std::string& name, bool defaultVal = false) {
    ParamDecl p;
    p.name = name;
    p.type = ParamType::Bool;
    p.defaultVal[0] = defaultVal ? 1.0f : 0.0f;
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

    /// Source line number (for VS Code decorations)
    int sourceLine = 0;

    /// Instance name in the chain
    std::string instanceName;
};

} // namespace vivid

// Macro to register an operator type
#define VIVID_OPERATOR(ClassName) \
    extern "C" vivid::Operator* create_##ClassName() { return new ClassName(); }
