#pragma once
#include "context.h"
#include <string>
#include <variant>

namespace vivid {

/**
 * @brief A parameter that can be either a constant value or a reference to another node's output.
 *
 * Use this in operators to support dynamic parameter binding. When set to a string
 * starting with "$", it references another node's output value.
 *
 * @code
 * class MyOperator : public Operator {
 *     ParamRef<float> scale_{4.0f};  // Default to constant 4.0
 *
 * public:
 *     // Fluent API supports both constants and references
 *     MyOperator& scale(float v) { scale_ = v; return *this; }
 *     MyOperator& scale(const std::string& ref) { scale_ = ref; return *this; }
 *
 *     void process(Context& ctx) override {
 *         float s = scale_.get(ctx);  // Resolves reference if needed
 *         // use s...
 *     }
 * };
 * @endcode
 */
template<typename T>
class ParamRef {
public:
    /// Create with a constant value
    ParamRef(T value = T{}) : data_(value) {}

    /// Create with a node reference (string starting with "$")
    ParamRef(const std::string& ref) : data_(ref) {}
    ParamRef(const char* ref) : data_(std::string(ref)) {}

    /// Assign a constant value
    ParamRef& operator=(T value) {
        data_ = value;
        return *this;
    }

    /// Assign a node reference
    ParamRef& operator=(const std::string& ref) {
        data_ = ref;
        return *this;
    }

    /// Check if this is a reference (vs constant)
    bool isReference() const {
        return std::holds_alternative<std::string>(data_);
    }

    /// Get the constant value (only valid if !isReference())
    T constant() const {
        return std::get<T>(data_);
    }

    /// Get the reference string (only valid if isReference())
    const std::string& reference() const {
        return std::get<std::string>(data_);
    }

    /// Resolve the value, reading from context if it's a reference
    T get(Context& ctx, T defaultValue = T{}) const {
        if (std::holds_alternative<T>(data_)) {
            return std::get<T>(data_);
        }

        const std::string& ref = std::get<std::string>(data_);
        if (ref.empty()) {
            return defaultValue;
        }

        // Parse reference: "$NodeName" or "$NodeName.output"
        std::string nodeName = ref;
        std::string outputName = "out";

        if (ref[0] == '$') {
            nodeName = ref.substr(1);
        }

        size_t dotPos = nodeName.find('.');
        if (dotPos != std::string::npos) {
            outputName = nodeName.substr(dotPos + 1);
            nodeName = nodeName.substr(0, dotPos);
        }

        return getFromContext(ctx, nodeName, outputName, defaultValue);
    }

private:
    std::variant<T, std::string> data_;

    // Specialization helpers
    T getFromContext(Context& ctx, const std::string& node,
                     const std::string& output, T defaultValue) const;
};

// Specialization for float
template<>
inline float ParamRef<float>::getFromContext(Context& ctx, const std::string& node,
                                              const std::string& output, float defaultValue) const {
    return ctx.getInputValue(node, output, defaultValue);
}

// Specialization for int
template<>
inline int ParamRef<int>::getFromContext(Context& ctx, const std::string& node,
                                          const std::string& output, int defaultValue) const {
    return static_cast<int>(ctx.getInputValue(node, output, static_cast<float>(defaultValue)));
}

// Specialization for bool
template<>
inline bool ParamRef<bool>::getFromContext(Context& ctx, const std::string& node,
                                            const std::string& output, bool defaultValue) const {
    return ctx.getInputValue(node, output, defaultValue ? 1.0f : 0.0f) > 0.5f;
}

/**
 * @brief Helper to create a float parameter reference
 */
inline ParamRef<float> floatRef(float value) { return ParamRef<float>(value); }
inline ParamRef<float> floatRef(const std::string& ref) { return ParamRef<float>(ref); }

/**
 * @brief Helper to create an int parameter reference
 */
inline ParamRef<int> intRef(int value) { return ParamRef<int>(value); }
inline ParamRef<int> intRef(const std::string& ref) { return ParamRef<int>(ref); }

/**
 * @brief Helper to create a bool parameter reference
 */
inline ParamRef<bool> boolRef(bool value) { return ParamRef<bool>(value); }
inline ParamRef<bool> boolRef(const std::string& ref) { return ParamRef<bool>(ref); }

} // namespace vivid
