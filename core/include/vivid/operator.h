#pragma once

// Vivid V3 - Operator Base Class
// Base class for all operators (effects, generators, etc.)

#include <string>
#include <vector>
#include <memory>

namespace vivid {

class Context;

// Output type for operators
enum class OutputKind {
    Texture,    // GPU texture output
    Value,      // Single float value
    ValueArray, // Array of float values
    Geometry    // 3D geometry (future)
};

// Parameter types for UI/serialization
enum class ParamType {
    Float, Int, Bool, Vec2, Vec3, Vec4, Color, String
};

// Parameter declaration for introspection
struct ParamDecl {
    std::string name;
    ParamType type;
    float minVal = 0.0f;
    float maxVal = 1.0f;
    float defaultVal[4] = {0, 0, 0, 0};
};

// State preservation for hot-reload
struct OperatorState {
    virtual ~OperatorState() = default;
};

// Texture state - preserves texture pixel data across hot-reload
struct TextureState : public OperatorState {
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;

    bool hasData() const { return !pixels.empty() && width > 0 && height > 0; }
};

// Base class for all operators
class Operator {
public:
    virtual ~Operator() = default;

    // Lifecycle
    virtual void init(Context& ctx) {}
    virtual void process(Context& ctx) = 0;
    virtual void cleanup() {}

    // Metadata
    virtual std::string name() const = 0;
    virtual OutputKind outputKind() const { return OutputKind::Texture; }
    virtual std::vector<ParamDecl> params() { return {}; }

    // State preservation (for hot reload)
    virtual std::unique_ptr<OperatorState> saveState() { return nullptr; }
    virtual void loadState(std::unique_ptr<OperatorState> state) {}

    // Input connections
    void setInput(Operator* op) { inputs_.push_back(op); }
    void setInput(int index, Operator* op) {
        if (index >= static_cast<int>(inputs_.size())) {
            inputs_.resize(index + 1, nullptr);
        }
        inputs_[index] = op;
    }
    Operator* getInput(int index = 0) const {
        return (index < static_cast<int>(inputs_.size())) ? inputs_[index] : nullptr;
    }
    size_t inputCount() const { return inputs_.size(); }

    // Source location (for editor)
    int sourceLine = 0;

protected:
    std::vector<Operator*> inputs_;
};

} // namespace vivid
