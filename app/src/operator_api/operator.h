#pragma once

#include "operator_api/types.h"
#include <vector>
#include <string>
#include <initializer_list>
#include <cstring>
#include <cmath>
#include <type_traits>

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
    const char*      widget_id           = nullptr;
    uint32_t         widget_span         = 0;

    // Optional semantic metadata for tooling/introspection.
    const char* semantic_tag    = nullptr;
    const char* semantic_shape  = nullptr;
    const char* semantic_unit   = nullptr;
    const char* semantic_intent = nullptr;
    const char* description    = nullptr;
    const char* asset_kind     = nullptr;
    const char* visible_when_param = nullptr;
    VividParamVisibilityOp visible_when_op = VIVID_PARAM_VIS_ALWAYS;
    std::vector<int32_t> visible_when_values;

    // Repeat-group metadata (for variadic port patterns)
    const char* repeat_group     = nullptr;
    uint16_t    repeat_group_idx = 0;
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
Param<T>& editor_only(Param<T>& p) {
    p.display_hint = VIVID_DISPLAY_EDITOR;
    return p;
}

// Mark a param as runtime scratch/computed state: hidden from the inspector and
// NOT written to the saved graph (it is recomputed at runtime), yet still
// readable/writable via MCP/API. Use for derived catalogs (e.g. plugin preset
// lists) and scratch command-input params that should never bloat saved files.
template<typename T>
Param<T>& transient_param(Param<T>& p) {
    p.display_hint = VIVID_DISPLAY_TRANSIENT;
    return p;
}

template<typename T>
Param<T>& param_widget(Param<T>& p, const char* widget_id, uint32_t widget_span) {
    p.widget_id = widget_id;
    p.widget_span = widget_span;
    return p;
}

template<typename T>
Param<T>& param_group(Param<T>& p, const char* group_name) {
    p.group = group_name;
    return p;
}

template<typename T>
Param<T>& repeat_group(Param<T>& p, const char* group_name, uint16_t idx) {
    p.repeat_group = group_name;
    p.repeat_group_idx = idx;
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

template<typename T>
Param<T>& description(Param<T>& p, const char* desc) {
    p.description = desc;
    return p;
}

template<typename T>
Param<T>& asset_kind(Param<T>& p, const char* kind) {
    p.asset_kind = kind;
    return p;
}

template<typename T, typename ControllerT>
Param<T>& visible_when_eq(Param<T>& p, const Param<ControllerT>& controller,
                          std::initializer_list<int32_t> values) {
    p.visible_when_param = controller.name;
    p.visible_when_op = VIVID_PARAM_VIS_EQ;
    p.visible_when_values.assign(values.begin(), values.end());
    return p;
}

template<typename T, typename ControllerT>
Param<T>& visible_when_ne(Param<T>& p, const Param<ControllerT>& controller,
                          std::initializer_list<int32_t> values) {
    p.visible_when_param = controller.name;
    p.visible_when_op = VIVID_PARAM_VIS_NE;
    p.visible_when_values.assign(values.begin(), values.end());
    return p;
}

template<typename T, typename ControllerT>
Param<T>& visible_when_eq(Param<T>& p, const Param<ControllerT>& controller,
                          int32_t value) {
    return visible_when_eq(p, controller, {value});
}

template<typename T, typename ControllerT>
Param<T>& visible_when_ne(Param<T>& p, const Param<ControllerT>& controller,
                          int32_t value) {
    return visible_when_ne(p, controller, {value});
}

template<typename T, typename ControllerT>
Param<T>& visible_when_in(Param<T>& p, const Param<ControllerT>& controller,
                          std::initializer_list<int32_t> values) {
    return visible_when_eq(p, controller, values);
}

template<typename T, typename ControllerT>
Param<T>& visible_when_not_in(Param<T>& p, const Param<ControllerT>& controller,
                              std::initializer_list<int32_t> values) {
    return visible_when_ne(p, controller, values);
}

template<typename T, typename ControllerT>
Param<T>& visible_when_in(Param<T>& p, const Param<ControllerT>& controller,
                          int32_t value) {
    return visible_when_eq(p, controller, value);
}

template<typename T, typename ControllerT>
Param<T>& visible_when_not_in(Param<T>& p, const Param<ControllerT>& controller,
                              int32_t value) {
    return visible_when_ne(p, controller, value);
}

template<typename T>
Param<T>& clear_visible_when(Param<T>& p) {
    p.visible_when_param = nullptr;
    p.visible_when_op = VIVID_PARAM_VIS_ALWAYS;
    p.visible_when_values.clear();
    return p;
}

inline VividPortDescriptor& semantic_tag(VividPortDescriptor& p, const char* tag) {
    p.semantic_tag = tag;
    return p;
}

inline VividPortDescriptor& semantic_shape(VividPortDescriptor& p, const char* shape) {
    p.semantic_shape = shape;
    return p;
}

inline VividPortDescriptor& semantic_intent(VividPortDescriptor& p, const char* intent) {
    p.semantic_intent = intent;
    return p;
}

inline VividPortDescriptor& description(VividPortDescriptor& p, const char* desc) {
    p.description = desc;
    return p;
}

inline VividPortDescriptor& repeat_group(VividPortDescriptor& p, const char* group_name, uint16_t idx) {
    p.repeat_group = group_name;
    p.repeat_group_idx = idx;
    return p;
}

// Mark a port as an advanced breakout — the inspector hides it on the node
// body unless a connection lands on it. Used for the standardized
// voice_*/voices_out per-voice surfaces on synths and for NoteBreakout's
// shared-control lanes. See docs/plans/midi-native-protocol/phase-2.
inline VividPortDescriptor& advanced_breakout(VividPortDescriptor& p) {
    p.display_hint = VIVID_PORT_DISPLAY_ADVANCED;
    return p;
}

// ---------------------------------------------------------------------------
// OperatorBase — abstract base class for operators (no process method)
// ---------------------------------------------------------------------------

struct OperatorBase {
    virtual ~OperatorBase() = default;
    virtual void collect_params(std::vector<ParamBase*>& out) = 0;
    virtual void collect_ports(std::vector<VividPortDescriptor>& out) = 0;
    // Optional one-time CPU-side warmup hook. The runtime calls this after
    // graph param/file-param values have been synced into the instance.
    // Heavy first-use cache building or initial file-backed asset decoding
    // belongs here, not in draw_thumbnail() or another UI-adjacent path.
    virtual void prepare_instance_assets() {}
    virtual void draw_thumbnail(const VividThumbnailContext*) {}  // optional override
    virtual void draw_inspector(VividInspectorContext*) {}        // optional override
    virtual void main_thread_update(double time) {}               // optional override
    // Audio-runtime role (v14+). Base = DEFAULT (classify by ports). The loaded-dylib adapter
    // overrides this to hand back the dylib's vivid_audio_role() export, so build_descriptor can
    // record it WITHOUT reading an appended field out of the (possibly older) dylib descriptor
    // struct. Built-in audio ops keep using the audio_op_mark_* tables, so they leave this DEFAULT.
    virtual VividAudioRole declared_audio_role() const { return VIVID_AUDIO_ROLE_DEFAULT; }
    // Operator role (v16+, ADR-0046). Base = DEFAULT (unclassified primitive). Compiled-in ops
    // override this to declare a role (e.g. a bundled generator returns RECIPE); the loaded-dylib
    // adapter overrides it to hand back the dylib's vivid_operator_role() export, so build_descriptor
    // records it WITHOUT reading an appended field out of the (possibly older) dylib descriptor struct.
    virtual VividOperatorRole declared_operator_role() const { return VIVID_OP_ROLE_DEFAULT; }
    // Host-internal: built-in operators store FILE/TEXT params as concrete Param<FilePath> /
    // Param<TextValue> members, so the host can sync resolved strings directly. Loaded dylib
    // operators mirror params as plain ParamBase descriptors and sync their real params inside the
    // ABI process call, so their adapter opts out.
    virtual bool host_syncs_file_params() const { return true; }
    // Host-internal: a built-in operator's process_* capabilities ARE its C++ interfaces, so the
    // host infers them by dynamic_cast (returns nullptr here). The loaded-dylib adapter implements
    // all three interfaces at once, so it can't be told apart that way — it overrides this to hand
    // back the dylib's own descriptor, whose has_process_* flags are the real capability set.
    // Operator authors never override this.
    virtual const VividOperatorDescriptor* host_capability_descriptor() const { return nullptr; }
};

// ---------------------------------------------------------------------------
// Capability interfaces — each operator implements exactly one to declare
// its fixed execution cadence: FrameProcessable (frame-rate), AudioProcessable
// (audio-rate), or GpuProcessable (GPU).
// ---------------------------------------------------------------------------

struct FrameProcessable {
    virtual ~FrameProcessable() = default;
    virtual void process_frame(const VividFrameContext* ctx) = 0;
};

struct AudioProcessable {
    virtual ~AudioProcessable() = default;
    virtual void process_audio(const VividAudioContext* ctx) = 0;
};

// NoteFlushable (ADR-0022 P3.3): an OPTIONAL capability for note-source operators (generators)
// that sustain voices across blocks. When the host stops calling a generator — e.g. its scene is
// no longer active — the generator can't emit its own note-offs, so the host asks it to flush:
// emit an off for every voice it is currently sounding into out[cap], setting *count. A separate
// interface (not a method on AudioProcessable) so it is ABI-additive — ops that don't implement it
// are simply never asked (the host caches a possibly-null NoteFlushable* per op, no vtable change).
struct NoteFlushable {
    virtual ~NoteFlushable() = default;
    virtual void note_flush(VividNoteEvent* out, uint32_t cap, uint32_t* count) = 0;
};

// GpuProcessable: forward-declared VividGpuContext* (defined in gpu_operator.h).
// GPU operators must #include "operator_api/gpu_operator.h" for the full definition.
struct GpuProcessable {
    virtual ~GpuProcessable() = default;
    virtual void process_gpu(const VividGpuContext* ctx) = 0;
};

// Append standard audio analysis output ports (rms, peak, waveform).
// Audio operators should call this at the end of collect_ports().
inline void append_analysis_ports(std::vector<VividPortDescriptor>& out) {
    auto make_analysis = [](const char* name, VividPortType type, VividPortTransport transport,
                            VividMultiplicity multiplicity = VIVID_MULTIPLICITY_SCALAR) {
        VividPortDescriptor pd{};
        pd.name = name;
        pd.type = type;
        pd.direction = VIVID_PORT_OUTPUT;
        pd.transport = transport;
        pd.semantic_tag = "analysis";
        pd.multiplicity = multiplicity;
        return pd;
    };
    out.push_back(make_analysis("rms",      VIVID_PORT_SCALAR, VIVID_PORT_TRANSPORT_SIGNAL));
    out.push_back(make_analysis("peak",     VIVID_PORT_SCALAR, VIVID_PORT_TRANSPORT_SIGNAL));
    out.push_back(make_analysis("waveform", VIVID_PORT_SCALAR, VIVID_PORT_TRANSPORT_SIGNAL, VIVID_MULTIPLICITY_MANY));
}

} // namespace vivid

namespace vivid { namespace detail {
template <typename T, typename = void>
struct has_strategy_independent : std::false_type {};
template <typename T>
struct has_strategy_independent<T, std::void_t<decltype(T::kStrategyIndependent)>> : std::true_type {};

template <typename T>
constexpr bool get_strategy_independent() {
    if constexpr (has_strategy_independent<T>::value)
        return T::kStrategyIndependent;
    else
        return false;
}

// Multiplicity behavior (the value-model authority). If the operator declares a
// static constexpr kMultiplicityBehavior, use it; otherwise default to Map (the
// pass-through behavior — an op that processes each value independently).
template <typename T, typename = void>
struct has_multiplicity_behavior : std::false_type {};
template <typename T>
struct has_multiplicity_behavior<T, std::void_t<decltype(T::kMultiplicityBehavior)>> : std::true_type {};

template <typename T>
constexpr VividMultiplicityBehavior get_multiplicity_behavior() {
    if constexpr (has_multiplicity_behavior<T>::value)
        return T::kMultiplicityBehavior;
    else
        return VIVID_MULTIPLICITY_MAP;
}
template <typename T, typename = void>
struct has_time_dependent : std::false_type {};
template <typename T>
struct has_time_dependent<T, std::void_t<decltype(T::kTimeDependent)>> : std::true_type {};

template <typename T>
constexpr bool get_time_dependent() {
    if constexpr (has_time_dependent<T>::value)
        return T::kTimeDependent;
    else
        return false;
}

// Audio role (v14+). If the operator declares a static constexpr kAudioRole, use it; otherwise
// DEFAULT (classify by ports). Lets an audio op (esp. a loaded dylib) mark itself a generator /
// note-effect / modulator the way built-ins are marked via audio_op_mark_* (see audio_op_runtime).
template <typename T, typename = void>
struct has_audio_role : std::false_type {};
template <typename T>
struct has_audio_role<T, std::void_t<decltype(T::kAudioRole)>> : std::true_type {};

template <typename T>
constexpr VividAudioRole get_audio_role() {
    if constexpr (has_audio_role<T>::value)
        return T::kAudioRole;
    else
        return VIVID_AUDIO_ROLE_DEFAULT;
}

// Operator role (v16+, ADR-0046). If the operator declares a static constexpr kRole, use it;
// otherwise DEFAULT. Lets an op (esp. a loaded dylib) mark itself a RECIPE / source / adapter / …
// the same way kAudioRole marks a generator / note-effect / modulator.
template <typename T, typename = void>
struct has_operator_role : std::false_type {};
template <typename T>
struct has_operator_role<T, std::void_t<decltype(T::kRole)>> : std::true_type {};

template <typename T>
constexpr VividOperatorRole get_operator_role() {
    if constexpr (has_operator_role<T>::value)
        return T::kRole;
    else
        return VIVID_OP_ROLE_DEFAULT;
}

// v3 metadata: display_name, keywords, summary. All optional — operators that
// don't declare these get auto-derived display name and empty keywords/summary.
template <typename T, typename = void>
struct has_display_name : std::false_type {};
template <typename T>
struct has_display_name<T, std::void_t<decltype(T::kDisplayName)>> : std::true_type {};

template <typename T>
constexpr const char* get_display_name() {
    if constexpr (has_display_name<T>::value)
        return T::kDisplayName;
    else
        return nullptr;
}

template <typename T, typename = void>
struct has_keywords : std::false_type {};
template <typename T>
struct has_keywords<T, std::void_t<decltype(T::kKeywords)>> : std::true_type {};

// kKeywords must be std::array<const char*, N> so .data()/.size() are available
// and the pointer storage is stable for the lifetime of the dylib.
template <typename T>
constexpr const char* const* get_keywords_data() {
    if constexpr (has_keywords<T>::value)
        return T::kKeywords.data();
    else
        return nullptr;
}

template <typename T>
constexpr uint32_t get_keywords_count() {
    if constexpr (has_keywords<T>::value)
        return static_cast<uint32_t>(T::kKeywords.size());
    else
        return 0;
}

template <typename T, typename = void>
struct has_summary : std::false_type {};
template <typename T>
struct has_summary<T, std::void_t<decltype(T::kSummary)>> : std::true_type {};

template <typename T>
constexpr const char* get_summary() {
    if constexpr (has_summary<T>::value)
        return T::kSummary;
    else
        return nullptr;
}

struct metadata_string_sink {
    metadata_string_sink& operator=(const char*) { return *this; }
};

struct metadata_keywords_sink {
    metadata_keywords_sink& operator=(std::initializer_list<const char*>) {
        return *this;
    }
};
}} // namespace vivid::detail

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
