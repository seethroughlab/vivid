#pragma once

#include "operator_api/types.h"
#include <vector>
#include <string>
#include <initializer_list>
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

    const char**   choice_labels = nullptr;
    uint32_t       choice_count  = 0;
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
    // Enum constructor with choice labels
    Param(const char* n, int def, std::initializer_list<const char*> labels) {
        name = n;
        type = VIVID_PARAM_INT;
        default_value = static_cast<float>(def);
        min_value = 0;
        max_value = static_cast<float>(labels.size() - 1);
        value = default_value;
        static_labels_.assign(labels.begin(), labels.end());
        choice_labels = static_labels_.data();
        choice_count = static_cast<uint32_t>(static_labels_.size());
    }
    int int_value() const { return static_cast<int>(value); }
private:
    std::vector<const char*> static_labels_;
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
// FilePath — tag type for file path parameters
// ---------------------------------------------------------------------------

struct FilePath {};

template<>
struct Param<FilePath> : ParamBase {
    std::string str_value;
    Param(const char* n, const char* def = "") {
        name = n;
        type = VIVID_PARAM_FILE;
        default_value = 0;
        min_value = 0;
        max_value = 0;
        value = 0;
        str_value = def;
    }
};

// ---------------------------------------------------------------------------
// OperatorBase — abstract base class for operators
// ---------------------------------------------------------------------------

struct OperatorBase {
    virtual ~OperatorBase() = default;
    virtual void collect_params(std::vector<ParamBase*>& out) = 0;
    virtual void collect_ports(std::vector<VividPortDescriptor>& out) = 0;
    virtual void process(const VividProcessContext* ctx) = 0;
    virtual void draw_thumbnail(const VividThumbnailContext*) {}  // optional override
    virtual void main_thread_update(double time) {}               // optional override
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
    static std::vector<std::vector<const char*>> s_label_storage;             \
    static std::vector<std::string> s_file_defaults;                          \
    static bool inited = false;                                               \
    if (!inited) {                                                            \
        inited = true;                                                        \
        ClassName tmp;                                                        \
        std::vector<vivid::ParamBase*> pbases;                                \
        tmp.collect_params(pbases);                                           \
        s_params.resize(pbases.size());                                       \
        s_label_storage.resize(pbases.size());                                \
        s_file_defaults.resize(pbases.size());                                 \
        for (size_t i = 0; i < pbases.size(); ++i) {                          \
            s_params[i].name          = pbases[i]->name;                      \
            s_params[i].type          = pbases[i]->type;                      \
            s_params[i].default_value = pbases[i]->default_value;             \
            s_params[i].min_value     = pbases[i]->min_value;                 \
            s_params[i].max_value     = pbases[i]->max_value;                 \
            if (pbases[i]->choice_count > 0) {                                \
                s_label_storage[i].assign(                                    \
                    pbases[i]->choice_labels,                                 \
                    pbases[i]->choice_labels + pbases[i]->choice_count);      \
                s_params[i].choice_labels = s_label_storage[i].data();        \
                s_params[i].choice_count  = pbases[i]->choice_count;          \
            } else {                                                          \
                s_params[i].choice_labels = nullptr;                          \
                s_params[i].choice_count  = 0;                                \
            }                                                                 \
            if (pbases[i]->type == VIVID_PARAM_FILE) {                        \
                auto* fp = static_cast<vivid::Param<vivid::FilePath>*>(       \
                    pbases[i]);                                               \
                s_file_defaults[i] = fp->str_value;                           \
                s_params[i].default_string = s_file_defaults[i].c_str();      \
            } else {                                                          \
                s_params[i].default_string = nullptr;                         \
            }                                                                 \
        }                                                                     \
        tmp.collect_ports(s_ports);                                           \
        desc.name           = ClassName::kName;                               \
        desc.domain         = ClassName::kDomain;                             \
        desc.param_count    = static_cast<uint32_t>(s_params.size());         \
        desc.params         = s_params.data();                                \
        desc.port_count     = static_cast<uint32_t>(s_ports.size());          \
        desc.ports          = s_ports.data();                                 \
        desc.time_dependent = ClassName::kTimeDependent ? 1 : 0;              \
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
    uint32_t file_idx = 0;                                                    \
    for (size_t i = 0; i < param_ptrs.size(); ++i) {                          \
        if (param_ptrs[i]->type == VIVID_PARAM_FILE) {                        \
            if (ctx->file_param_values && file_idx < ctx->file_param_count) { \
                auto* fp = static_cast<vivid::Param<vivid::FilePath>*>(       \
                    param_ptrs[i]);                                           \
                if (ctx->file_param_values[file_idx])                         \
                    fp->str_value = ctx->file_param_values[file_idx];         \
            }                                                                 \
            file_idx++;                                                       \
        } else {                                                              \
            param_ptrs[i]->value = ctx->param_values[i];                      \
        }                                                                     \
    }                                                                         \
    op->process(ctx);                                                         \
}                                                                             \
                                                                              \
extern "C" void vivid_main_thread_update(void* instance, double time,         \
                                         const char** file_param_values,       \
                                         uint32_t file_param_count) {          \
    auto* op = static_cast<ClassName*>(instance);                              \
    std::vector<vivid::ParamBase*> param_ptrs = _vivid_collect_param_ptrs(op); \
    uint32_t file_idx = 0;                                                    \
    for (size_t i = 0; i < param_ptrs.size(); ++i) {                          \
        if (param_ptrs[i]->type == VIVID_PARAM_FILE) {                        \
            if (file_param_values && file_idx < file_param_count) {           \
                auto* fp = static_cast<vivid::Param<vivid::FilePath>*>(       \
                    param_ptrs[i]);                                           \
                if (file_param_values[file_idx])                              \
                    fp->str_value = file_param_values[file_idx];              \
            }                                                                 \
            file_idx++;                                                       \
        }                                                                     \
    }                                                                         \
    op->main_thread_update(time);                                             \
}

// ---------------------------------------------------------------------------
// VIVID_THUMBNAIL(ClassName) — exports vivid_draw_thumbnail entry point
// Place alongside VIVID_REGISTER for operators that override draw_thumbnail.
// ---------------------------------------------------------------------------

#define VIVID_THUMBNAIL(ClassName)                                             \
extern "C" void vivid_draw_thumbnail(void* instance,                           \
                                     const VividThumbnailContext* ctx) {        \
    static_cast<ClassName*>(instance)->draw_thumbnail(ctx);                     \
}
