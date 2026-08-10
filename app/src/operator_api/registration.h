#pragma once
/* Operator registration + export machinery — extracted from operator.h. Holds the authoring
   shorthand macros (VIVID_PARAMS / VIVID_PORTS / vivid_lane_state) and the extern "C" ABI
   scaffolding macros (VIVID_INTERNAL_EXPORTS_WITH_DESCRIPTOR for the codegen path, and the
   standalone VIVID_REGISTER for package operators). These are TEXT macros: they expand in a
   translation unit that has already included operator.h, so they reference vivid::OperatorBase /
   Param / the vivid::detail traits without this header needing to include operator.h (which
   would be circular). Included by operator.h at the end; not meant to be included alone. */
#include "operator_api/operator_metadata.h"

// ---------------------------------------------------------------------------
// VIVID_PARAMS / VIVID_PORTS — shorthand for simple param/port registration
// ---------------------------------------------------------------------------

#define VIVID_PARAMS(...)                                                \
    void collect_params(std::vector<vivid::ParamBase*>& out) override {  \
        out = {__VA_ARGS__};                                             \
    }

#define VIVID_PORTS(...)                                                  \
    void collect_ports(std::vector<VividPortDescriptor>& out) override {  \
        out = {__VA_ARGS__};                                              \
    }

// Convenience macro: get identity-keyed per-lane persistent state from audio context.
// Returns T* pointing to zero-initialized storage stable until lane retirement.
#define vivid_lane_state(ctx, lane_id, T) \
    static_cast<T*>((ctx)->lane_state_fn((ctx)->lane_state_service, (lane_id), sizeof(T)))

// ---------------------------------------------------------------------------
// VIVID_INTERNAL_EXPORTS_WITH_DESCRIPTOR — shared ABI scaffolding
//
// Used by operator_codegen-generated registration files. Defines _VividInstance,
// _vivid_sync_params, and all required extern "C" entry points. The generated
// file supplies the descriptor expression and "v2" mode literal.
// ---------------------------------------------------------------------------

#define VIVID_INTERNAL_EXPORTS_WITH_DESCRIPTOR(ClassName, DescriptorExpr, ModeLiteral) \
                                                                              \
struct _VividInstance {                                                        \
    ClassName op;                                                              \
    std::vector<vivid::ParamBase*> param_ptrs;                                \
};                                                                            \
                                                                              \
/* Helper: sync param values from ctx fields into operator Param<> members */ \
static void _vivid_sync_params(_VividInstance* inst, float* param_values,      \
                               const char** file_param_values,                 \
                               uint32_t file_param_count) {                    \
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
        } else if (param_values) {                                            \
            param_ptrs[i]->value = param_values[i];                           \
        }                                                                     \
    }                                                                         \
}                                                                             \
                                                                              \
extern "C" uint32_t vivid_abi_version() {                                     \
    return VIVID_OPERATOR_ABI_VERSION;                                        \
}                                                                             \
                                                                              \
extern "C" const char* vivid_registration_mode() {                            \
    return ModeLiteral;                                                       \
}                                                                             \
                                                                              \
extern "C" const VividOperatorDescriptor* vivid_descriptor() {                \
    return (DescriptorExpr);                                                  \
}                                                                             \
                                                                              \
extern "C" void* vivid_create() {                                             \
    auto* inst = new _VividInstance();                                        \
    inst->op.collect_params(inst->param_ptrs);                                \
    return inst;                                                              \
}                                                                             \
                                                                              \
extern "C" void vivid_destroy(void* instance) {                               \
    delete static_cast<_VividInstance*>(instance);                            \
}                                                                             \
                                                                              \
template<typename _Op>                                                        \
static void _vivid_dispatch_control(void* instance,                           \
                                    VividFrameContext* ctx) {                 \
    if constexpr (std::is_base_of_v<vivid::FrameProcessable, _Op>) {          \
        auto* inst = static_cast<_VividInstance*>(instance);                  \
        _vivid_sync_params(inst, ctx->param_values,                           \
                           ctx->file_param_values, ctx->file_param_count);    \
        static_cast<_Op&>(inst->op).process_frame(ctx);                       \
    }                                                                         \
}                                                                             \
template<typename _Op>                                                        \
static void _vivid_dispatch_audio(void* instance,                             \
                                  VividAudioContext* ctx) {                   \
    if constexpr (std::is_base_of_v<vivid::AudioProcessable, _Op>) {          \
        auto* inst = static_cast<_VividInstance*>(instance);                  \
        /* Skip TEXT/FILE params — vivid_main_thread_update() already syncs   \
         * them on the frame thread. Copying large strings (e.g. pattern_data)\
         * on every audio callback is a real-time safety violation. */        \
        _vivid_sync_params(inst, ctx->param_values, nullptr, 0);              \
        static_cast<_Op&>(inst->op).process_audio(ctx);                       \
    }                                                                         \
}                                                                             \
template<typename _Op, typename _Ctx>                                         \
static void _vivid_dispatch_gpu(void* instance, _Ctx* ctx) {                  \
    if constexpr (std::is_base_of_v<vivid::GpuProcessable, _Op>) {            \
        auto* inst = static_cast<_VividInstance*>(instance);                  \
        _vivid_sync_params(inst, ctx->param_values,                           \
                           ctx->file_param_values, ctx->file_param_count);    \
        static_cast<_Op&>(inst->op).process_gpu(ctx);                         \
    }                                                                         \
}                                                                             \
                                                                              \
extern "C" void vivid_process_frame(void* instance,                           \
                                    VividFrameContext* ctx) {                 \
    _vivid_dispatch_control<ClassName>(instance, ctx);                        \
}                                                                             \
                                                                              \
extern "C" void vivid_process_audio(void* instance,                           \
                                    VividAudioContext* ctx) {                 \
    _vivid_dispatch_audio<ClassName>(instance, ctx);                          \
}                                                                             \
                                                                              \
extern "C" void vivid_process_gpu(void* instance,                             \
                                  VividGpuContext* ctx) {                     \
    _vivid_dispatch_gpu<ClassName>(instance, ctx);                            \
}                                                                             \
                                                                              \
extern "C" void vivid_main_thread_update(void* instance, double time,         \
                                         std::string** file_param_strings,    \
                                         uint32_t file_param_count) {         \
    auto* inst = static_cast<_VividInstance*>(instance);                      \
    /* Sync text/file param values FROM runtime storage INTO operator */       \
    {                                                                          \
        uint32_t file_idx = 0;                                                \
        for (size_t i = 0; i < inst->param_ptrs.size(); ++i) {                \
            auto* pb = inst->param_ptrs[i];                                   \
            if (pb->type == VIVID_PARAM_FILE || pb->type == VIVID_PARAM_TEXT) { \
                if (file_param_strings && file_idx < file_param_count &&      \
                    file_param_strings[file_idx]) {                           \
                    if (pb->type == VIVID_PARAM_FILE) {                       \
                        static_cast<vivid::Param<vivid::FilePath>*>(pb)       \
                            ->str_value = *file_param_strings[file_idx];      \
                    } else {                                                   \
                        static_cast<vivid::Param<vivid::TextValue>*>(pb)      \
                            ->str_value = *file_param_strings[file_idx];      \
                    }                                                          \
                }                                                             \
                ++file_idx;                                                   \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    inst->op.main_thread_update(time);                                        \
    /* Write back TextValue str_values so list_*_params can read them */      \
    {                                                                          \
        uint32_t file_idx = 0;                                                \
        for (size_t i = 0; i < inst->param_ptrs.size(); ++i) {                \
            auto* pb = inst->param_ptrs[i];                                   \
            if (pb->type == VIVID_PARAM_FILE || pb->type == VIVID_PARAM_TEXT) { \
                if (pb->type == VIVID_PARAM_TEXT &&                           \
                    file_param_strings && file_idx < file_param_count &&      \
                    file_param_strings[file_idx]) {                           \
                    *file_param_strings[file_idx] =                           \
                        static_cast<vivid::Param<vivid::TextValue>*>(pb)      \
                            ->str_value;                                      \
                }                                                             \
                ++file_idx;                                                   \
            }                                                                 \
        }                                                                     \
    }                                                                         \
}                                                                             \
                                                                              \
extern "C" void vivid_prepare_instance_assets(                                \
    void* instance,                                                           \
    const float* param_values,                                                \
    const char** file_param_values,                                           \
    uint32_t file_param_count) {                                              \
    auto* inst = static_cast<_VividInstance*>(instance);                      \
    _vivid_sync_params(inst, const_cast<float*>(param_values),                \
                       file_param_values, file_param_count);                  \
    inst->op.prepare_instance_assets();                                       \
}

// ---------------------------------------------------------------------------
// VIVID_REGISTER(ClassName) — standalone registration for package operators
//
// Self-contained alternative to operator_codegen. Suitable for packages
// that don't run the codegen tool. Uses VIVID_INTERNAL_EXPORTS_WITH_DESCRIPTOR
// is not available outside this macro.
//
// Usage: place at file scope after the operator struct definition.
//   VIVID_REGISTER(MyOp)
//
// No-op when VIVID_CODEGEN_ACTIVE is defined (i.e., when the source is
// included from a codegen-generated registration file — in that case the
// generated file itself provides all extern "C" entry points).
// ---------------------------------------------------------------------------

#ifdef VIVID_CODEGEN_ACTIVE
#define VIVID_REGISTER(ClassName)
#else
#define VIVID_REGISTER(ClassName)                                             \
                                                                              \
static const VividOperatorDescriptor* _vivid_get_descriptor() {               \
    static VividOperatorDescriptor desc{};                                    \
    static std::vector<VividParamDescriptor> s_params;                        \
    static std::vector<VividPortDescriptor>  s_ports;                         \
    static std::vector<std::vector<std::string>> s_label_storage;             \
    static std::vector<std::vector<const char*>> s_label_ptrs;                \
    static std::vector<std::string> s_file_defaults;                          \
    static std::vector<std::vector<int32_t>> s_visibility_values;             \
    static bool inited = false;                                               \
    if (!inited) {                                                            \
        inited = true;                                                        \
        static ClassName tmp;                                                  \
        std::vector<vivid::ParamBase*> pbases;                                \
        tmp.collect_params(pbases);                                           \
        s_params.resize(pbases.size());                                       \
        s_label_storage.resize(pbases.size());                                \
        s_label_ptrs.resize(pbases.size());                                   \
        s_file_defaults.resize(pbases.size());                                 \
        s_visibility_values.resize(pbases.size());                            \
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
            s_params[i].widget_id           = pbases[i]->widget_id;            \
            s_params[i].widget_span         = pbases[i]->widget_span;          \
            s_params[i].semantic_tag        = pbases[i]->semantic_tag;         \
            s_params[i].semantic_shape      = pbases[i]->semantic_shape;       \
            s_params[i].semantic_unit       = pbases[i]->semantic_unit;        \
            s_params[i].semantic_intent     = pbases[i]->semantic_intent;      \
            s_params[i].description         = pbases[i]->description;          \
            s_params[i].asset_kind          = pbases[i]->asset_kind;           \
            s_params[i].repeat_group        = pbases[i]->repeat_group;          \
            s_params[i].repeat_group_idx    = pbases[i]->repeat_group_idx;     \
            s_params[i].visible_when_param  = pbases[i]->visible_when_param;   \
            s_params[i].visible_when_op     = pbases[i]->visible_when_op;      \
            s_visibility_values[i]          = pbases[i]->visible_when_values;  \
            s_params[i].visible_when_values = s_visibility_values[i].empty()   \
                ? nullptr : s_visibility_values[i].data();                    \
            s_params[i].visible_when_value_count =                            \
                static_cast<uint32_t>(s_visibility_values[i].size());         \
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
        desc.display_name   = vivid::detail::get_display_name<ClassName>();    \
        desc.keywords       = vivid::detail::get_keywords_data<ClassName>();   \
        desc.keyword_count  = vivid::detail::get_keywords_count<ClassName>();  \
        desc.summary        = vivid::detail::get_summary<ClassName>();         \
        desc.has_process_audio =                                              \
            std::is_base_of_v<vivid::AudioProcessable, ClassName> ? 1 : 0;    \
        desc.has_process_gpu =                                                \
            std::is_base_of_v<vivid::GpuProcessable, ClassName> ? 1 : 0;      \
        desc.has_process_frame =                                              \
            std::is_base_of_v<vivid::FrameProcessable, ClassName> ? 1 : 0;    \
        desc.multiplicity_behavior =                                          \
            vivid::detail::get_multiplicity_behavior<ClassName>();             \
        desc.strategy_independent =                                           \
            vivid::detail::get_strategy_independent<ClassName>() ? 1 : 0;     \
        desc.param_count    = static_cast<uint32_t>(s_params.size());         \
        desc.params         = s_params.data();                                \
        desc.port_count     = static_cast<uint32_t>(s_ports.size());          \
        desc.ports          = s_ports.data();                                 \
        desc.time_dependent =                                                 \
            vivid::detail::get_time_dependent<ClassName>() ? 1 : 0;           \
        desc.audio_role =                                                     \
            vivid::detail::get_audio_role<ClassName>();                       \
        desc.role =                                                           \
            vivid::detail::get_operator_role<ClassName>();                    \
    }                                                                         \
    return &desc;                                                             \
}                                                                             \
                                                                              \
struct _VividInstance {                                                        \
    ClassName op;                                                              \
    std::vector<vivid::ParamBase*> param_ptrs;                                \
};                                                                            \
                                                                              \
static void _vivid_sync_params(_VividInstance* inst, float* param_values,      \
                               const char** file_param_values,                 \
                               uint32_t file_param_count) {                    \
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
        } else if (param_values) {                                            \
            param_ptrs[i]->value = param_values[i];                           \
        }                                                                     \
    }                                                                         \
}                                                                             \
                                                                              \
extern "C" uint32_t vivid_abi_version() {                                     \
    return VIVID_OPERATOR_ABI_VERSION;                                        \
}                                                                             \
extern "C" const VividOperatorDescriptor* vivid_descriptor() {                \
    return _vivid_get_descriptor();                                           \
}                                                                             \
extern "C" void* vivid_create() {                                             \
    auto* inst = new _VividInstance();                                        \
    inst->op.collect_params(inst->param_ptrs);                                \
    return inst;                                                              \
}                                                                             \
extern "C" void vivid_destroy(void* instance) {                               \
    delete static_cast<_VividInstance*>(instance);                            \
}                                                                             \
template<typename _Op>                                                        \
static void _vivid_dispatch_control(void* instance,                           \
                                    VividFrameContext* ctx) {                 \
    if constexpr (std::is_base_of_v<vivid::FrameProcessable, _Op>) {          \
        auto* inst = static_cast<_VividInstance*>(instance);                  \
        _vivid_sync_params(inst, ctx->param_values,                           \
                           ctx->file_param_values, ctx->file_param_count);    \
        static_cast<_Op&>(inst->op).process_frame(ctx);                       \
    }                                                                         \
}                                                                             \
template<typename _Op>                                                        \
static void _vivid_dispatch_audio(void* instance,                             \
                                  VividAudioContext* ctx) {                   \
    if constexpr (std::is_base_of_v<vivid::AudioProcessable, _Op>) {          \
        auto* inst = static_cast<_VividInstance*>(instance);                  \
        /* Skip TEXT/FILE params — vivid_main_thread_update() already syncs   \
         * them on the frame thread. Copying large strings (e.g. pattern_data)\
         * on every audio callback is a real-time safety violation. */        \
        _vivid_sync_params(inst, ctx->param_values, nullptr, 0);              \
        static_cast<_Op&>(inst->op).process_audio(ctx);                       \
    }                                                                         \
}                                                                             \
template<typename _Op, typename _Ctx>                                         \
static void _vivid_dispatch_gpu(void* instance, _Ctx* ctx) {                  \
    if constexpr (std::is_base_of_v<vivid::GpuProcessable, _Op>) {            \
        auto* inst = static_cast<_VividInstance*>(instance);                  \
        _vivid_sync_params(inst, ctx->param_values,                           \
                           ctx->file_param_values, ctx->file_param_count);    \
        static_cast<_Op&>(inst->op).process_gpu(ctx);                         \
    }                                                                         \
}                                                                             \
extern "C" void vivid_process_frame(void* instance,                           \
                                    VividFrameContext* ctx) {                 \
    _vivid_dispatch_control<ClassName>(instance, ctx);                        \
}                                                                             \
extern "C" void vivid_process_audio(void* instance,                           \
                                    VividAudioContext* ctx) {                 \
    _vivid_dispatch_audio<ClassName>(instance, ctx);                          \
}                                                                             \
extern "C" void vivid_process_gpu(void* instance,                             \
                                  VividGpuContext* ctx) {                     \
    _vivid_dispatch_gpu<ClassName>(instance, ctx);                            \
}                                                                             \
/* v14: optional operator-drawn thumbnail. draw_thumbnail is on OperatorBase  \
 * (base no-op), so this always resolves — the host dlsym's it and calls it   \
 * for a loaded dylib's cell preview. */                                      \
extern "C" void vivid_draw_thumbnail(void* instance,                          \
                                     const VividThumbnailContext* ctx) {      \
    static_cast<_VividInstance*>(instance)->op.draw_thumbnail(ctx);           \
}                                                                             \
/* v14: optional audio-role hint (generator / note-effect / modulator). */    \
extern "C" uint32_t vivid_audio_role(void) {                                  \
    return static_cast<uint32_t>(vivid::detail::get_audio_role<ClassName>()); \
}                                                                             \
/* v16 (ADR-0046): optional operator-role hint (recipe / source / adapter …).*/\
extern "C" uint32_t vivid_operator_role(void) {                               \
    return static_cast<uint32_t>(vivid::detail::get_operator_role<ClassName>());\
}                                                                             \
extern "C" void vivid_main_thread_update(void* instance, double time,         \
                                         std::string** file_param_strings,    \
                                         uint32_t file_param_count) {         \
    auto* inst = static_cast<_VividInstance*>(instance);                      \
    {                                                                          \
        uint32_t file_idx = 0;                                                \
        for (size_t i = 0; i < inst->param_ptrs.size(); ++i) {                \
            auto* pb = inst->param_ptrs[i];                                   \
            if (pb->type == VIVID_PARAM_FILE || pb->type == VIVID_PARAM_TEXT) { \
                if (file_param_strings && file_idx < file_param_count &&      \
                    file_param_strings[file_idx]) {                           \
                    if (pb->type == VIVID_PARAM_FILE) {                       \
                        static_cast<vivid::Param<vivid::FilePath>*>(pb)       \
                            ->str_value = *file_param_strings[file_idx];      \
                    } else {                                                   \
                        static_cast<vivid::Param<vivid::TextValue>*>(pb)      \
                            ->str_value = *file_param_strings[file_idx];      \
                    }                                                          \
                }                                                             \
                ++file_idx;                                                   \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    inst->op.main_thread_update(time);                                        \
    {                                                                          \
        uint32_t file_idx = 0;                                                \
        for (size_t i = 0; i < inst->param_ptrs.size(); ++i) {                \
            auto* pb = inst->param_ptrs[i];                                   \
            if (pb->type == VIVID_PARAM_FILE || pb->type == VIVID_PARAM_TEXT) { \
                if (pb->type == VIVID_PARAM_TEXT &&                           \
                    file_param_strings && file_idx < file_param_count &&      \
                    file_param_strings[file_idx]) {                           \
                    *file_param_strings[file_idx] =                           \
                        static_cast<vivid::Param<vivid::TextValue>*>(pb)      \
                            ->str_value;                                      \
                }                                                             \
                ++file_idx;                                                   \
            }                                                                 \
        }                                                                     \
    }                                                                         \
}                                                                             \
extern "C" void vivid_prepare_instance_assets(                                \
    void* instance,                                                           \
    const float* param_values,                                                \
    const char** file_param_values,                                           \
    uint32_t file_param_count) {                                              \
    auto* inst = static_cast<_VividInstance*>(instance);                      \
    _vivid_sync_params(inst, const_cast<float*>(param_values),                \
                       file_param_values, file_param_count);                  \
    inst->op.prepare_instance_assets();                                       \
}
#endif // VIVID_CODEGEN_ACTIVE

// Metadata block consumed by operator_codegen. The body intentionally compiles
// as a no-op function while preserving the ergonomic syntax:
//
//   VIVID_DEFINE_OP(MyOp) {
//       name = "MyOp";
//       keywords = {"foo", "bar"};
//   }
#define VIVID_DEFINE_OP(ClassName)                                            \
    static void vivid_operator_metadata_##ClassName(                          \
        [[maybe_unused]] vivid::detail::metadata_string_sink name,            \
        [[maybe_unused]] vivid::detail::metadata_string_sink display_name,    \
        [[maybe_unused]] vivid::detail::metadata_keywords_sink keywords,      \
        [[maybe_unused]] vivid::detail::metadata_string_sink summary)


// ---------------------------------------------------------------------------
// VIVID_FILE_DROP(handlers_array) — exports vivid_file_drop_descriptor.
// handlers_array must be a static array of VividFileDropHandlerDescriptor.
// Does not reference _VividInstance; safe under codegen.
// ---------------------------------------------------------------------------

#define VIVID_FILE_DROP(handlers_array)                                        \
extern "C" const VividFileDropHandlerDescriptor* vivid_file_drop_descriptor(   \
    uint32_t* count) {                                                         \
    if (count)                                                                 \
        *count = static_cast<uint32_t>(sizeof(handlers_array) /                \
                                       sizeof((handlers_array)[0]));           \
    return (handlers_array);                                                   \
}

// ---------------------------------------------------------------------------
// VIVID_THUMBNAIL / VIVID_INSPECTOR / VIVID_INSPECTOR_FULL_MODE / VIVID_EDITOR
//
// Always expand to nothing in operator source — the generated registration file
// emits these exports itself (after _VividInstance is defined).
// ---------------------------------------------------------------------------

#define VIVID_THUMBNAIL(ClassName)
#define VIVID_INSPECTOR(ClassName)
#define VIVID_INSPECTOR_FULL_MODE(ClassName)
#define VIVID_EDITOR(ClassName)
