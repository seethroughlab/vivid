#ifndef VIVID_OPERATOR_API_OPERATOR_H
#define VIVID_OPERATOR_API_OPERATOR_H

#include "operator_api/types.h"
#include <vector>
#include <cstring>
#include <cmath>

namespace vivid {

// ---------------------------------------------------------------------------
// ParamBase — type-erased parameter metadata
// ---------------------------------------------------------------------------

struct ParamBase {
    const char*    name;
    VividParamType type;
    float          default_value;
    float          min_value;
    float          max_value;

    // Current value stored as float (bool/int reinterpreted)
    float value;
};

// ---------------------------------------------------------------------------
// Param<T> — typed parameter
// ---------------------------------------------------------------------------

template<typename T>
struct Param;

template<>
struct Param<float> : ParamBase {
    Param(const char* n, float def, float lo, float hi) {
        name = n;
        type = VIVID_PARAM_FLOAT;
        default_value = def;
        min_value = lo;
        max_value = hi;
        value = def;
    }
};

template<>
struct Param<int> : ParamBase {
    Param(const char* n, int def, int lo, int hi) {
        name = n;
        type = VIVID_PARAM_INT;
        default_value = static_cast<float>(def);
        min_value = static_cast<float>(lo);
        max_value = static_cast<float>(hi);
        value = default_value;
    }
    int int_value() const { return static_cast<int>(value); }
};

template<>
struct Param<bool> : ParamBase {
    Param(const char* n, bool def) {
        name = n;
        type = VIVID_PARAM_BOOL;
        default_value = def ? 1.0f : 0.0f;
        min_value = 0.0f;
        max_value = 1.0f;
        value = default_value;
    }
    bool bool_value() const { return value > 0.5f; }
};

// ---------------------------------------------------------------------------
// OperatorBase — abstract base class for operators
// ---------------------------------------------------------------------------

struct OperatorBase {
    virtual ~OperatorBase() = default;
    virtual void collect_params(std::vector<ParamBase*>& out) = 0;
    virtual void collect_ports(std::vector<VividPortDescriptor>& out) = 0;
    virtual void process(const VividProcessContext* ctx) = 0;
};

} // namespace vivid

// ---------------------------------------------------------------------------
// VIVID_REGISTER(ClassName) — generates extern "C" entry points
// ---------------------------------------------------------------------------

#define VIVID_REGISTER(ClassName)                                             \
                                                                              \
static const VividOperatorDescriptor* _vivid_get_descriptor() {               \
    static VividOperatorDescriptor desc{};                                    \
    static std::vector<VividParamDescriptor> s_params;                        \
    static std::vector<VividPortDescriptor>  s_ports;                         \
    static bool inited = false;                                               \
    if (!inited) {                                                            \
        inited = true;                                                        \
        ClassName tmp;                                                        \
        std::vector<vivid::ParamBase*> pbases;                                \
        tmp.collect_params(pbases);                                           \
        s_params.resize(pbases.size());                                       \
        for (size_t i = 0; i < pbases.size(); ++i) {                          \
            s_params[i].name          = pbases[i]->name;                      \
            s_params[i].type          = pbases[i]->type;                      \
            s_params[i].default_value = pbases[i]->default_value;             \
            s_params[i].min_value     = pbases[i]->min_value;                 \
            s_params[i].max_value     = pbases[i]->max_value;                 \
        }                                                                     \
        tmp.collect_ports(s_ports);                                           \
        desc.name        = ClassName::kName;                                  \
        desc.domain      = ClassName::kDomain;                                \
        desc.param_count = static_cast<uint32_t>(s_params.size());            \
        desc.params      = s_params.data();                                   \
        desc.port_count  = static_cast<uint32_t>(s_ports.size());             \
        desc.ports       = s_ports.data();                                    \
    }                                                                         \
    return &desc;                                                             \
}                                                                             \
                                                                              \
static std::vector<vivid::ParamBase*> _vivid_collect_param_ptrs(              \
        ClassName* op) {                                                      \
    std::vector<vivid::ParamBase*> ptrs;                                      \
    op->collect_params(ptrs);                                                 \
    return ptrs;                                                              \
}                                                                             \
                                                                              \
extern "C" const VividOperatorDescriptor* vivid_descriptor() {                \
    return _vivid_get_descriptor();                                           \
}                                                                             \
                                                                              \
extern "C" void* vivid_create() {                                             \
    return new ClassName();                                                    \
}                                                                             \
                                                                              \
extern "C" void vivid_destroy(void* instance) {                               \
    delete static_cast<ClassName*>(instance);                                  \
}                                                                             \
                                                                              \
extern "C" void vivid_process(void* instance,                                 \
                              const VividProcessContext* ctx) {                \
    auto* op = static_cast<ClassName*>(instance);                              \
    std::vector<vivid::ParamBase*> param_ptrs = _vivid_collect_param_ptrs(op); \
    for (size_t i = 0; i < param_ptrs.size(); ++i) {                          \
        param_ptrs[i]->value = ctx->param_values[i];                          \
    }                                                                         \
    op->process(ctx);                                                         \
}

#endif // VIVID_OPERATOR_API_OPERATOR_H
