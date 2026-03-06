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

    // Inspector layout metadata
    const char*      group               = nullptr;
    VividDisplayHint display_hint        = VIVID_DISPLAY_DEFAULT;
    uint8_t          layout_columns      = 0;
    uint8_t          layout_column_index = 0;

    // Optional semantic metadata for tooling/introspection.
    const char* semantic_tag    = nullptr;
    const char* semantic_shape  = nullptr;
    const char* semantic_unit   = nullptr;
    const char* semantic_intent = nullptr;
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
        label_ptrs_.assign(labels.begin(), labels.end());
        choice_labels = label_ptrs_.data();
        choice_count = static_cast<uint32_t>(label_ptrs_.size());
    }
    Param(const char* n, int def, const std::vector<std::string>& labels) {
        name = n;
        type = VIVID_PARAM_INT;
        default_value = static_cast<float>(def);
        min_value = 0;
        max_value = labels.empty() ? 0.0f : static_cast<float>(labels.size() - 1);
        value = default_value;
        owned_labels_ = labels;
        label_ptrs_.clear();
        label_ptrs_.reserve(owned_labels_.size());
        for (const auto& s : owned_labels_) label_ptrs_.push_back(s.c_str());
        choice_labels = label_ptrs_.empty() ? nullptr : label_ptrs_.data();
        choice_count = static_cast<uint32_t>(label_ptrs_.size());
    }
    int int_value() const { return static_cast<int>(value); }
private:
    std::vector<std::string> owned_labels_;
    std::vector<const char*> label_ptrs_;
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
struct TextValue {};

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

template<>
struct Param<TextValue> : ParamBase {
    std::string str_value;
    Param(const char* n, const char* def = "") {
        name = n;
        type = VIVID_PARAM_TEXT;
        default_value = 0;
        min_value = 0;
        max_value = 0;
        value = 0;
        str_value = def;
    }
};

// ---------------------------------------------------------------------------
// Helper functions for setting inspector layout metadata on params
// ---------------------------------------------------------------------------

template<typename T>
Param<T>& display_hint(Param<T>& p, VividDisplayHint hint) {
    p.display_hint = hint;
    return p;
}

template<typename T>
Param<T>& param_group(Param<T>& p, const char* group_name) {
    p.group = group_name;
    return p;
}

template<typename T>
Param<T>& layout_row(Param<T>& p, uint8_t columns, uint8_t col) {
    p.layout_columns = columns;
    p.layout_column_index = col;
    return p;
}

template<typename T>
Param<T>& semantic_tag(Param<T>& p, const char* tag) {
    p.semantic_tag = tag;
    return p;
}

template<typename T>
Param<T>& semantic_shape(Param<T>& p, const char* shape) {
    p.semantic_shape = shape;
    return p;
}

template<typename T>
Param<T>& semantic_unit(Param<T>& p, const char* unit) {
    p.semantic_unit = unit;
    return p;
}

template<typename T>
Param<T>& semantic_intent(Param<T>& p, const char* intent) {
    p.semantic_intent = intent;
    return p;
}

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
struct _VividInstance {                                                        \
    ClassName op;                                                              \
    std::vector<vivid::ParamBase*> param_ptrs;                                \
};                                                                            \
                                                                              \
static const VividOperatorDescriptor* _vivid_get_descriptor() {               \
    static VividOperatorDescriptor desc{};                                    \
    static std::vector<VividParamDescriptor> s_params;                        \
    static std::vector<VividPortDescriptor>  s_ports;                         \
    static std::vector<std::vector<std::string>> s_label_storage;             \
    static std::vector<std::vector<const char*>> s_label_ptrs;                \
    static std::vector<std::string> s_file_defaults;                          \
    static bool inited = false;                                               \
    if (!inited) {                                                            \
        inited = true;                                                        \
        ClassName tmp;                                                        \
        std::vector<vivid::ParamBase*> pbases;                                \
        tmp.collect_params(pbases);                                           \
        s_params.resize(pbases.size());                                       \
        s_label_storage.resize(pbases.size());                                \
        s_label_ptrs.resize(pbases.size());                                   \
        s_file_defaults.resize(pbases.size());                                 \
        for (size_t i = 0; i < pbases.size(); ++i) {                          \
            s_params[i].name          = pbases[i]->name;                      \
            s_params[i].type          = pbases[i]->type;                      \
            s_params[i].default_value = pbases[i]->default_value;             \
            s_params[i].min_value     = pbases[i]->min_value;                 \
            s_params[i].max_value     = pbases[i]->max_value;                 \
            s_params[i].group               = pbases[i]->group;                \
            s_params[i].display_hint        = pbases[i]->display_hint;         \
            s_params[i].layout_columns      = pbases[i]->layout_columns;       \
            s_params[i].layout_column_index = pbases[i]->layout_column_index;  \
            s_params[i].semantic_tag        = pbases[i]->semantic_tag;         \
            s_params[i].semantic_shape      = pbases[i]->semantic_shape;       \
            s_params[i].semantic_unit       = pbases[i]->semantic_unit;        \
            s_params[i].semantic_intent     = pbases[i]->semantic_intent;      \
            if (pbases[i]->choice_count > 0) {                                \
                s_label_storage[i].clear();                                   \
                s_label_ptrs[i].clear();                                      \
                s_label_storage[i].reserve(pbases[i]->choice_count);          \
                s_label_ptrs[i].reserve(pbases[i]->choice_count);             \
                for (uint32_t li = 0; li < pbases[i]->choice_count; ++li) {   \
                    const char* src = pbases[i]->choice_labels[li];           \
                    s_label_storage[i].emplace_back(src ? src : "");          \
                }                                                              \
                for (const auto& lbl : s_label_storage[i])                    \
                    s_label_ptrs[i].push_back(lbl.c_str());                   \
                s_params[i].choice_labels = s_label_ptrs[i].data();           \
                s_params[i].choice_count  = pbases[i]->choice_count;          \
            } else {                                                          \
                s_params[i].choice_labels = nullptr;                          \
                s_params[i].choice_count  = 0;                                \
            }                                                                 \
            if (pbases[i]->type == VIVID_PARAM_FILE ||                         \
                pbases[i]->type == VIVID_PARAM_TEXT) {                         \
                const std::string* strp = nullptr;                             \
                if (pbases[i]->type == VIVID_PARAM_FILE) {                     \
                    auto* fp = static_cast<vivid::Param<vivid::FilePath>*>(    \
                        pbases[i]);                                            \
                    strp = &fp->str_value;                                     \
                } else {                                                       \
                    auto* tp = static_cast<vivid::Param<vivid::TextValue>*>(   \
                        pbases[i]);                                            \
                    strp = &tp->str_value;                                     \
                }                                                              \
                s_file_defaults[i] = *strp;                                    \
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
extern "C" const VividOperatorDescriptor* vivid_descriptor() {                \
    return _vivid_get_descriptor();                                           \
}                                                                             \
                                                                              \
extern "C" void* vivid_create() {                                             \
    auto* inst = new _VividInstance();                                         \
    inst->op.collect_params(inst->param_ptrs);                                \
    return inst;                                                              \
}                                                                             \
                                                                              \
extern "C" void vivid_destroy(void* instance) {                               \
    delete static_cast<_VividInstance*>(instance);                             \
}                                                                             \
                                                                              \
extern "C" void vivid_process(void* instance,                                 \
                              const VividProcessContext* ctx) {                \
    auto* inst = static_cast<_VividInstance*>(instance);                       \
    auto& param_ptrs = inst->param_ptrs;                                      \
    uint32_t file_idx = 0;                                                    \
    for (size_t i = 0; i < param_ptrs.size(); ++i) {                          \
        if (param_ptrs[i]->type == VIVID_PARAM_FILE ||                        \
            param_ptrs[i]->type == VIVID_PARAM_TEXT) {                        \
            if (ctx->file_param_values && file_idx < ctx->file_param_count) { \
                if (ctx->file_param_values[file_idx]) {                       \
                    if (param_ptrs[i]->type == VIVID_PARAM_FILE) {            \
                        auto* fp = static_cast<vivid::Param<vivid::FilePath>*>(\
                            param_ptrs[i]);                                   \
                        fp->str_value = ctx->file_param_values[file_idx];     \
                    } else {                                                   \
                        auto* tp = static_cast<vivid::Param<vivid::TextValue>*>(\
                            param_ptrs[i]);                                   \
                        tp->str_value = ctx->file_param_values[file_idx];     \
                    }                                                          \
                }                                                              \
            }                                                                 \
            file_idx++;                                                       \
        } else {                                                              \
            param_ptrs[i]->value = ctx->param_values[i];                      \
        }                                                                     \
    }                                                                         \
    inst->op.process(ctx);                                                    \
}                                                                             \
                                                                              \
extern "C" void vivid_main_thread_update(void* instance, double time,         \
                                         const char** file_param_values,       \
                                         uint32_t file_param_count) {          \
    auto* inst = static_cast<_VividInstance*>(instance);                       \
    auto& param_ptrs = inst->param_ptrs;                                      \
    uint32_t file_idx = 0;                                                    \
    for (size_t i = 0; i < param_ptrs.size(); ++i) {                          \
        if (param_ptrs[i]->type == VIVID_PARAM_FILE ||                        \
            param_ptrs[i]->type == VIVID_PARAM_TEXT) {                        \
            if (file_param_values && file_idx < file_param_count) {           \
                if (file_param_values[file_idx]) {                            \
                    if (param_ptrs[i]->type == VIVID_PARAM_FILE) {            \
                        auto* fp = static_cast<vivid::Param<vivid::FilePath>*>(\
                            param_ptrs[i]);                                   \
                        fp->str_value = file_param_values[file_idx];          \
                    } else {                                                   \
                        auto* tp = static_cast<vivid::Param<vivid::TextValue>*>(\
                            param_ptrs[i]);                                   \
                        tp->str_value = file_param_values[file_idx];          \
                    }                                                          \
                }                                                              \
            }                                                                 \
            file_idx++;                                                       \
        }                                                                     \
    }                                                                         \
    inst->op.main_thread_update(time);                                        \
}

// ---------------------------------------------------------------------------
// VIVID_THUMBNAIL(ClassName) — exports vivid_draw_thumbnail entry point
// Place alongside VIVID_REGISTER for operators that override draw_thumbnail.
// ---------------------------------------------------------------------------

#define VIVID_THUMBNAIL(ClassName)                                             \
extern "C" void vivid_draw_thumbnail(void* instance,                           \
                                     const VividThumbnailContext* ctx) {        \
    static_cast<_VividInstance*>(instance)->op.draw_thumbnail(ctx);             \
}
