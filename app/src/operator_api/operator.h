#pragma once

#include "operator_api/types.h"
#include "operator_api/ports.h"   // authoring helpers: texture/audio port builders + param readers
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

// The metadata-detection traits and the registration/export macros used to live inline below;
// they now live in their own headers to keep this author-facing header readable. operator.h
// still includes them here (at the end, after the types above are defined) so every operator
// that includes "operator_api/operator.h" compiles exactly as before.
#include "operator_api/operator_metadata.h"
#include "operator_api/registration.h"
