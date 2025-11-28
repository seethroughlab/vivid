#pragma once
#include "context.h"
#include "types.h"
#include <memory>
#include <vector>
#include <string>

namespace vivid {

// Base class for serializable operator state (for hot-reload)
struct OperatorState {
    virtual ~OperatorState() = default;
};

// Base class for all operators
class Operator {
public:
    virtual ~Operator() = default;

    // Lifecycle
    virtual void init(Context& ctx) {}
    virtual void process(Context& ctx) = 0;
    virtual void cleanup() {}

    // Hot reload support
    virtual std::unique_ptr<OperatorState> saveState() { return nullptr; }
    virtual void loadState(std::unique_ptr<OperatorState> state) {}

    // Introspection for editor
    virtual std::vector<ParamDecl> params() { return {}; }
    virtual OutputKind outputKind() { return OutputKind::Texture; }

    // Identity (set by NODE macro)
    void setId(const std::string& id) { id_ = id; }
    void setSourceLine(int line) { sourceLine_ = line; }
    const std::string& id() const { return id_; }
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
