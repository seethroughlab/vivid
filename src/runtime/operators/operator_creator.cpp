#include "runtime/operators/operator_creator.h"
#include "runtime/operators/operator_registry.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include "runtime/platform/process_runner.h"

namespace vivid {
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string to_pascal_case(const std::string& snake) {
    std::string result;
    bool capitalize_next = true;
    for (char c : snake) {
        if (c == '_') {
            capitalize_next = true;
        } else {
            result += capitalize_next ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            capitalize_next = false;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Template string replacement
// ---------------------------------------------------------------------------

static std::string tpl_replace(std::string s, const std::string& key, const std::string& val) {
    for (size_t pos = 0; (pos = s.find(key, pos)) != std::string::npos; pos += val.size())
        s.replace(pos, key.size(), val);
    return s;
}

// Shared tail comment included at the end of every generated operator. Gives
// authors concrete examples they can uncomment / adapt instead of hunting
// through the reference docs.
//
// The semantic-tag example is parameterised per-environment because the
// default Param name differs (amount for control/gpu, gain for audio).
//   %SEMANTIC_TAG_EXAMPLE_PARAM%  — param identifier (no quotes).
//   %SEMANTIC_TAG_EXAMPLE_TAG%    — canonical tag string (quoted in the output).
static constexpr const char* kOptionalFeaturesComment = R"(
    // --- Optional features (uncomment or add as needed) ---
    // static constexpr bool kTimeDependent = true;           // ctx->time, ctx->delta_time
    //
    // Multiplicity (value model): declare how this operator transforms "many"
    // values — one of Map / Reduce / Generate / Collect / Preserve / Kernel:
    //   static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_MAP;
    //
    // Many-valued I/O — the value API (#include "operator_api/value_view.h"):
    //   read:   const float* v = vivid_value_floats(&ctx->values[port]);
    //           uint32_t     n = vivid_value_count(&ctx->values[port]);
    //   write:  float* out = vivid_value_output_floats(&ctx->value_outputs[port], n);
    //           for (uint32_t i = 0; i < n; ++i) out[i] = /* ... */;
    //           vivid_value_output_commit(&ctx->value_outputs[port], n);
    //   (strings: vivid_value_strings / vivid_value_output_set_string)
    //
    // Param types:   int, bool, FilePath, TextValue (e.g. vivid::Param[T])
    // Port types:    VIVID_PORT_STRING, VIVID_PORT_TEXTURE, VIVID_PORT_AUDIO_BUFFER
    //                (a many-valued output currently declares VIVID_PORT_LANE_ARRAY)
    //
    // Semantic metadata (populated in the ctor or collect_params):
    //   vivid::semantic_tag(%SEMANTIC_TAG_EXAMPLE_PARAM%, "%SEMANTIC_TAG_EXAMPLE_TAG%");
    //   vivid::semantic_shape(%SEMANTIC_TAG_EXAMPLE_PARAM%, "scalar");
    //
    // Thumbnails:    void draw_thumbnail(const VividDrawContext*) override;
    // Child ops:     vivid::ChildOp<LFO> lfo_;  vivid::ChildOp<Smooth> smoother_;
    // Per-element state:  vivid_lane_state() — identity-keyed persistent storage
    //
    // One-shot setup (called once per instance before first process_*):
    //   void prepare_instance_assets() override { /* load dylib assets, …*/ })";

static bool is_valid_identifier(const std::string& name) {
    if (name.empty()) return false;
    // Must start with lowercase letter
    if (!std::islower(static_cast<unsigned char>(name[0]))) return false;
    for (char c : name) {
        if (c != '_' && !std::islower(static_cast<unsigned char>(c)) &&
            !std::isdigit(static_cast<unsigned char>(c)))
            return false;
    }
    // No double underscores, no leading/trailing underscore
    if (name.front() == '_' || name.back() == '_') return false;
    if (name.find("__") != std::string::npos) return false;
    return true;
}

static bool is_valid_cpp_type_name(const std::string& name) {
    if (name.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
        return false;
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
            return false;
    }
    return true;
}

static bool is_valid_stable_type_id_literal(const std::string& stable_type_id) {
    if (stable_type_id.empty()) return false;
    if (stable_type_id.front() == '.' || stable_type_id.back() == '.')
        return false;
    bool saw_namespace_sep = false;
    for (char c : stable_type_id) {
        if (c == '.') {
            saw_namespace_sep = true;
            continue;
        }
        if (c == '_') continue;
        if (std::islower(static_cast<unsigned char>(c))) continue;
        if (std::isdigit(static_cast<unsigned char>(c))) continue;
        return false;
    }
    return saw_namespace_sep;
}

static const char* port_type_name(VividPortType t) {
    switch (t) {
        case VIVID_PORT_SCALAR:         return "VIVID_PORT_SCALAR";
        case VIVID_PORT_AUDIO_BUFFER:         return "VIVID_PORT_AUDIO_BUFFER";
        case VIVID_PORT_LANE_ARRAY:        return "VIVID_PORT_LANE_ARRAY";
        case VIVID_PORT_STRING:        return "VIVID_PORT_STRING";
        case VIVID_PORT_STRING_LANES: return "VIVID_PORT_STRING_LANES";
        case VIVID_PORT_TEXTURE:       return "VIVID_PORT_TEXTURE";
        default:                       return "VIVID_PORT_SCALAR";
    }
}

static bool is_custom_port_spec(const VividPortSpec& p) {
    return !p.stable_type_id.empty();
}

static bool has_custom_port_fields(const VividPortSpec& p) {
    return !p.type_name.empty() ||
           p.payload_size > 0 ||
           p.audio_safe ||
           p.transport == VIVID_PORT_TRANSPORT_CUSTOM_REF ||
           p.transport == VIVID_PORT_TRANSPORT_CUSTOM_VALUE;
}

static std::string custom_port_decl(const VividPortSpec& p) {
    const char* dir = (p.direction == VIVID_PORT_INPUT) ? "VIVID_PORT_INPUT" : "VIVID_PORT_OUTPUT";
    if (p.transport == VIVID_PORT_TRANSPORT_CUSTOM_VALUE)
        return "        out.push_back(VIVID_CUSTOM_VALUE_PORT(\"" + p.name + "\", " + dir + ", " + p.type_name + "));\n";
    return "        out.push_back(VIVID_CUSTOM_REF_PORT(\"" + p.name + "\", " + dir + ", " + p.type_name + "));\n";
}

static std::string build_custom_type_support(const std::vector<VividPortSpec>& ports) {
    std::vector<VividPortSpec> custom_types;
    for (const auto& p : ports) {
        if (!is_custom_port_spec(p)) continue;
        bool seen = false;
        for (const auto& existing : custom_types) {
            if (existing.type_name == p.type_name &&
                existing.stable_type_id == p.stable_type_id) {
                seen = true;
                break;
            }
        }
        if (!seen) custom_types.push_back(p);
    }
    if (custom_types.empty()) return {};

    std::ostringstream s;
    s << "#include \"operator_api/type_id.h\"\n";
    s << "#include \"operator_api/port_type_registry.h\"\n";
    s << "#include <cstdint>\n\n";

    for (const auto& p : custom_types) {
        s << "struct " << p.type_name << " {\n";
        if (p.payload_size > 0) {
            s << "    uint8_t bytes[" << p.payload_size << "] = {};\n";
        } else {
            s << "    uint8_t bytes[16] = {};\n";
        }
        s << "};\n";
        if (p.transport == VIVID_PORT_TRANSPORT_CUSTOM_VALUE) {
            s << "VIVID_DECLARE_CUSTOM_VALUE_TYPE(" << p.type_name << ", \""
              << p.stable_type_id << "\", \"" << p.type_name << "\", "
              << (p.audio_safe ? "true" : "false") << ");\n\n";
        } else {
            s << "VIVID_DECLARE_CUSTOM_REF_TYPE(" << p.type_name << ", \""
              << p.stable_type_id << "\", \"" << p.type_name << "\", "
              << (p.audio_safe ? "true" : "false") << ");\n\n";
        }
    }

    s << "extern \"C\" const VividPortTypeInfo* vivid_describe_custom_types(uint32_t* count) {\n";
    s << "    static const VividPortTypeInfo kInfos[] = {\n";
    for (size_t i = 0; i < custom_types.size(); ++i) {
        s << "        vivid_custom_type_info<" << custom_types[i].type_name << ">()";
        s << (i + 1 < custom_types.size() ? ",\n" : "\n");
    }
    s << "    };\n";
    s << "    *count = static_cast<uint32_t>(sizeof(kInfos) / sizeof(kInfos[0]));\n";
    s << "    return kInfos;\n";
    s << "}\n\n";
    return s.str();
}

static const char* kind_subdir(VividOperatorKind k) {
    switch (k) {
        case VIVID_OP_CONTROL: return "control";
        case VIVID_OP_AUDIO: return "audio";
        case VIVID_OP_GPU:   return "gpu";
        default: return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Param codegen helpers
// ---------------------------------------------------------------------------

static std::string param_type_cpp(VividParamType t) {
    switch (t) {
        case VIVID_PARAM_FLOAT: return "float";
        case VIVID_PARAM_INT:   return "int";
        case VIVID_PARAM_BOOL:  return "bool";
        case VIVID_PARAM_FILE:  return "vivid::FilePath";
        case VIVID_PARAM_TEXT:  return "vivid::TextValue";
        default:                return "float";
    }
}

// Format a float literal so it always has a decimal point (e.g. "1.0f", not "1f").
static std::string float_literal(float v) {
    std::ostringstream fs;
    fs << v;
    std::string r = fs.str();
    if (r.find('.') == std::string::npos && r.find('e') == std::string::npos)
        r += ".0";
    r += 'f';
    return r;
}

static std::string build_param_decl(const VividParamSpec& p) {
    std::ostringstream s;
    s << "    vivid::Param<" << param_type_cpp(p.type) << "> " << p.name << "{\"" << p.name << "\"";
    switch (p.type) {
        case VIVID_PARAM_FLOAT:
            s << ", " << float_literal(p.default_value) << ", " << float_literal(p.min_value) << ", " << float_literal(p.max_value);
            break;
        case VIVID_PARAM_INT:
            s << ", " << static_cast<int>(p.default_value)
              << ", " << static_cast<int>(p.min_value)
              << ", " << static_cast<int>(p.max_value);
            break;
        case VIVID_PARAM_BOOL:
            s << ", " << (p.default_value != 0.0f ? "true" : "false");
            break;
        case VIVID_PARAM_FILE:
            break;
        case VIVID_PARAM_TEXT:
            s << ", \"" << p.default_string << "\"";
            break;
    }
    s << "};\n";
    return s.str();
}

static std::string build_param_decls(const std::vector<VividParamSpec>& params) {
    std::string result;
    for (const auto& p : params)
        result += build_param_decl(p);
    return result;
}

// ---------------------------------------------------------------------------
// Port collection helpers
// ---------------------------------------------------------------------------

static std::string build_params_macro(const std::vector<VividParamSpec>& params) {
    // Emit an explicit collect_params override (not the VIVID_PARAMS shorthand
    // macro) so the scaffolded source reads the same way seasoned operators do
    // and tooling that greps for collect_params finds it.
    std::ostringstream s;
    s << "    void collect_params(std::vector<vivid::ParamBase*>& out) override {\n";
    for (const auto& p : params) {
        s << "        out.push_back(&" << p.name << ");\n";
    }
    s << "    }\n";
    return s.str();
}

static std::string build_ports_macro(const std::vector<VividPortSpec>& ports) {
    std::ostringstream s;

    // Custom port types can't use the VIVID_PORTS shorthand — fall back to manual.
    bool has_custom = false;
    for (const auto& p : ports)
        if (is_custom_port_spec(p)) { has_custom = true; break; }

    if (has_custom) {
        s << "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n";
        for (const auto& p : ports) {
            if (is_custom_port_spec(p)) {
                s << custom_port_decl(p);
            } else {
                const char* dir = (p.direction == VIVID_PORT_INPUT) ? "VIVID_PORT_INPUT" : "VIVID_PORT_OUTPUT";
                s << "        out.push_back({\"" << p.name << "\", " << port_type_name(p.type) << ", " << dir << "});\n";
            }
        }
        s << "    }\n";
        return s.str();
    }

    // Simple case — explicit collect_ports, same shape as the custom branch
    // above. Keeps the scaffolded source readable and greppable.
    s << "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n";
    for (const auto& p : ports) {
        const char* dir = (p.direction == VIVID_PORT_INPUT)
            ? "VIVID_PORT_INPUT" : "VIVID_PORT_OUTPUT";
        s << "        out.push_back({\"" << p.name << "\", "
          << port_type_name(p.type) << ", " << dir << "});\n";
    }
    s << "    }\n";
    return s.str();
}

// Count ports by direction
static int count_ports(const std::vector<VividPortSpec>& ports, VividPortDirection dir) {
    int n = 0;
    for (const auto& p : ports)
        if (p.direction == dir) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// Raw string literal templates
// ---------------------------------------------------------------------------

static constexpr const char* kControlTemplate = R"(#include "operator_api/operator.h"

// Operator guide: https://vivid.dev/docs/operator-api

%CUSTOM_TYPE_SUPPORT%struct %STRUCT% : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "%STRUCT%";
    // Optional v3 metadata. kDisplayName is the human-facing label the chooser
    // and inspector show; the runtime auto-derives "%STRUCT%" by splitting
    // CamelCase/snake_case if you leave kDisplayName unset, so most operators
    // need it only to override acronyms (FmSynth -> "FM Synth"). kKeywords
    // surfaces synonyms a user might type that don't appear in the name;
    // kSummary is a one-line description for the chooser preview + MCP catalog.
    // static constexpr const char* kDisplayName = "%STRUCT%";
    // static constexpr std::array<const char*, 2> kKeywords = {"keyword_one", "keyword_two"};
    // static constexpr const char* kSummary = "One-line description.";

%PARAM_DECLS%
%PARAMS_MACRO%
%PORTS_MACRO%
    void process_frame(const VividFrameContext* ctx) override {
%PROCESS_BODY%    }
%OPTIONAL_FEATURES%
};
)";

static constexpr const char* kAudioTemplate = R"(#include "operator_api/operator.h"

// Operator guide: https://vivid.dev/docs/operator-api

%CUSTOM_TYPE_SUPPORT%struct %STRUCT% : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "%STRUCT%";
    // Optional v3 metadata. kDisplayName is the human-facing label the chooser
    // and inspector show; the runtime auto-derives "%STRUCT%" by splitting
    // CamelCase/snake_case if you leave kDisplayName unset, so most operators
    // need it only to override acronyms (FmSynth -> "FM Synth"). kKeywords
    // surfaces synonyms a user might type that don't appear in the name;
    // kSummary is a one-line description for the chooser preview + MCP catalog.
    // static constexpr const char* kDisplayName = "%STRUCT%";
    // static constexpr std::array<const char*, 2> kKeywords = {"keyword_one", "keyword_two"};
    // static constexpr const char* kSummary = "One-line description.";
    static constexpr bool kTimeDependent = true;

%PARAM_DECLS%
%PARAMS_MACRO%
%PORTS_MACRO%
    // Audio-thread contract: no heap alloc, no locks, no blocking I/O.
    void process_audio(const VividAudioContext* ctx) override {
%PROCESS_BODY%    }
%OPTIONAL_FEATURES%
};
)";

static constexpr const char* kGpuTemplate = R"(#include "operator_api/wgsl_filter.h"

// Operator guide: https://vivid.dev/docs/operator-api

%CUSTOM_TYPE_SUPPORT%struct %STRUCT% : vivid::WgslFilterBase {
    static constexpr const char* kName = "%STRUCT%";
    // Optional v3 metadata. kDisplayName is the human-facing label the chooser
    // and inspector show; the runtime auto-derives "%STRUCT%" by splitting
    // CamelCase/snake_case if you leave kDisplayName unset, so most operators
    // need it only to override acronyms (FmSynth -> "FM Synth"). kKeywords
    // surfaces synonyms a user might type that don't appear in the name;
    // kSummary is a one-line description for the chooser preview + MCP catalog.
    // static constexpr const char* kDisplayName = "%STRUCT%";
    // static constexpr std::array<const char*, 2> kKeywords = {"keyword_one", "keyword_two"};
    // static constexpr const char* kSummary = "One-line description.";
    static constexpr bool kTimeDependent = true;

%PARAM_DECLS%
    %STRUCT%() : WgslFilterBase("%NAME%.wgsl") {}

%PORTS_MACRO%
%PARAMS_MACRO%
%EXTRA_OUTPUTS%
%OPTIONAL_FEATURES%
};

)";

static constexpr const char* kChildOpTemplate = R"(#include "operator_api/child_op.h"
#include "control/lfo/lfo.h"
#include "control/smooth/smooth.h"

// Operator guide: https://vivid.dev/docs/operator-api

// ChildOp<T> embeds another operator as a private member with its own state,
// giving this operator owned, host-local behavior from the embedded type
// (phase, smoothing history, etc.) without going through the graph.
//
// Embeddable operators are either header-only or exposed via
// vivid_embeddable_op_support through a *_embeddable.cpp support file — see
// operators/control/lfo/ and operators/control/smooth/ for the pattern. When
// building this operator, add EXTRA_LIBS vivid_embeddable_op_support to its
// cmake entry.

struct %STRUCT% : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "%STRUCT%";
    // Optional v3 metadata. kDisplayName is the human-facing label the chooser
    // and inspector show; the runtime auto-derives "%STRUCT%" by splitting
    // CamelCase/snake_case if you leave kDisplayName unset, so most operators
    // need it only to override acronyms (FmSynth -> "FM Synth"). kKeywords
    // surfaces synonyms a user might type that don't appear in the name;
    // kSummary is a one-line description for the chooser preview + MCP catalog.
    // static constexpr const char* kDisplayName = "%STRUCT%";
    // static constexpr std::array<const char*, 2> kKeywords = {"keyword_one", "keyword_two"};
    // static constexpr const char* kSummary = "One-line description.";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> amount   {"amount",    1.0f, 0.0f, 10.0f};
    vivid::Param<float> lfo_rate {"lfo_rate",  2.0f, 0.01f, 20.0f};
    vivid::Param<float> lfo_depth{"lfo_depth", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> smooth_time{"smooth_time", 0.05f, 0.0f, 2.0f};

    vivid::ChildOp<LFO>    lfo_;
    vivid::ChildOp<Smooth> smoother_;

    %STRUCT%() {
        // Semantic metadata — drives UI formatting and modulation routing.
        vivid::semantic_tag(lfo_rate, "frequency_hz");
        vivid::semantic_unit(lfo_rate, "Hz");
        vivid::semantic_tag(smooth_time, "time_seconds");
        vivid::semantic_unit(smooth_time, "s");
    }

    VIVID_PARAMS(&amount, &lfo_rate, &lfo_depth, &smooth_time)
    VIVID_PORTS(
        {"input",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT},
        {"output", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT}
    )

    void process_frame(const VividFrameContext* ctx) override {
        float input = ctx->input_values[0];

        // Drive internal LFO
        lfo_.set_param("frequency", lfo_rate.value);
        lfo_.set_param("amplitude", 1.0f);
        lfo_.process(ctx);

        // Smooth the LFO output
        smoother_.set_param("rise_time", smooth_time.value);
        smoother_.set_param("fall_time", smooth_time.value);
        smoother_.set_input("input", lfo_.output("value"));
        smoother_.process(ctx);

        float mod = smoother_.output("value");
        ctx->output_values[0] = input * (amount.value + lfo_depth.value * mod);
    }
};

)";

static constexpr const char* kEmptyControlTemplate = R"(#include "operator_api/operator.h"

// Operator guide: https://vivid.dev/docs/operator-api

struct %STRUCT% : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "%STRUCT%";
    // Optional v3 metadata. kDisplayName is the human-facing label the chooser
    // and inspector show; the runtime auto-derives "%STRUCT%" by splitting
    // CamelCase/snake_case if you leave kDisplayName unset, so most operators
    // need it only to override acronyms (FmSynth -> "FM Synth"). kKeywords
    // surfaces synonyms a user might type that don't appear in the name;
    // kSummary is a one-line description for the chooser preview + MCP catalog.
    // static constexpr const char* kDisplayName = "%STRUCT%";
    // static constexpr std::array<const char*, 2> kKeywords = {"keyword_one", "keyword_two"};
    // static constexpr const char* kSummary = "One-line description.";

    void collect_params(std::vector<vivid::ParamBase*>& /*out*/) override {
        // No params in the empty variant — add Param members above and
        // push_back them here.
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0];
    }
%OPTIONAL_FEATURES%
};

)";

static constexpr const char* kEmptyAudioTemplate = R"(#include "operator_api/operator.h"

// Operator guide: https://vivid.dev/docs/operator-api

struct %STRUCT% : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "%STRUCT%";
    // Optional v3 metadata. kDisplayName is the human-facing label the chooser
    // and inspector show; the runtime auto-derives "%STRUCT%" by splitting
    // CamelCase/snake_case if you leave kDisplayName unset, so most operators
    // need it only to override acronyms (FmSynth -> "FM Synth"). kKeywords
    // surfaces synonyms a user might type that don't appear in the name;
    // kSummary is a one-line description for the chooser preview + MCP catalog.
    // static constexpr const char* kDisplayName = "%STRUCT%";
    // static constexpr std::array<const char*, 2> kKeywords = {"keyword_one", "keyword_two"};
    // static constexpr const char* kSummary = "One-line description.";
    static constexpr bool kTimeDependent = true;

    void collect_params(std::vector<vivid::ParamBase*>& /*out*/) override {
        // No params in the empty variant.
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
    }

    // Audio-thread contract: no heap alloc, no locks, no blocking I/O.
    void process_audio(const VividAudioContext* ctx) override {
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        for (uint32_t i = 0; i < ctx->buffer_size; i++)
            out[i] = in[i];
    }
%OPTIONAL_FEATURES%
};

)";

static constexpr const char* kEmptyGpuTemplate = R"(#include "operator_api/wgsl_filter.h"

// Operator guide: https://vivid.dev/docs/operator-api

struct %STRUCT% : vivid::WgslFilterBase {
    static constexpr const char* kName = "%STRUCT%";
    // Optional v3 metadata. kDisplayName is the human-facing label the chooser
    // and inspector show; the runtime auto-derives "%STRUCT%" by splitting
    // CamelCase/snake_case if you leave kDisplayName unset, so most operators
    // need it only to override acronyms (FmSynth -> "FM Synth"). kKeywords
    // surfaces synonyms a user might type that don't appear in the name;
    // kSummary is a one-line description for the chooser preview + MCP catalog.
    // static constexpr const char* kDisplayName = "%STRUCT%";
    // static constexpr std::array<const char*, 2> kKeywords = {"keyword_one", "keyword_two"};
    // static constexpr const char* kSummary = "One-line description.";
    static constexpr bool kTimeDependent = true;

    %STRUCT%() : WgslFilterBase("%NAME%.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& /*out*/) override {
        // No params in the empty variant.
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }
%OPTIONAL_FEATURES%
};

)";

static constexpr const char* kGpuShaderTemplate = R"(// Starter shader. See GPU examples in operators/gpu/ for advanced patterns.

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);
    return mix(color, vec4f(color.rgb * u.amount, color.a), u.amount);
}
)";

// ---------------------------------------------------------------------------
// Template functions — build dynamic parts and substitute into raw templates
// ---------------------------------------------------------------------------

static std::string control_template(const std::string& name, const std::string& struct_name,
                                     const std::vector<VividPortSpec>& ports,
                                     const std::vector<VividParamSpec>& params) {
    bool custom_ports  = !ports.empty();
    bool custom_params = !params.empty();

    std::vector<VividPortSpec> effective_ports;
    if (custom_ports) {
        effective_ports = ports;
    } else {
        effective_ports.push_back({"input",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        effective_ports.push_back({"output", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    std::vector<VividParamSpec> effective_params;
    if (custom_params) {
        effective_params = params;
    } else {
        effective_params.push_back({"amount", VIVID_PARAM_FLOAT, 1.0f, 0.0f, 1.0f});
    }

    int num_inputs  = count_ports(effective_ports, VIVID_PORT_INPUT);
    int num_outputs = count_ports(effective_ports, VIVID_PORT_OUTPUT);

    // Build process body
    std::ostringstream body;
    if (num_inputs > 0 && num_outputs > 0) {
        if (custom_params) {
            body << "        ctx->output_values[0] = ctx->input_values[0];\n";
        } else {
            body << "        ctx->output_values[0] = ctx->input_values[0] * amount.value;\n";
        }
        for (int i = 1; i < num_outputs; ++i)
            body << "        ctx->output_values[" << i << "] = 0.0f;  // TODO\n";
    } else if (num_outputs > 0) {
        for (int i = 0; i < num_outputs; ++i)
            body << "        ctx->output_values[" << i << "] = 0.0f;  // TODO\n";
    }

    std::string s = kControlTemplate;
    s = tpl_replace(s, "%STRUCT%", struct_name);
    s = tpl_replace(s, "%CUSTOM_TYPE_SUPPORT%", build_custom_type_support(effective_ports));
    s = tpl_replace(s, "%PARAM_DECLS%", build_param_decls(effective_params));
    s = tpl_replace(s, "%PARAMS_MACRO%", build_params_macro(effective_params));
    s = tpl_replace(s, "%PORTS_MACRO%", build_ports_macro(effective_ports));
    s = tpl_replace(s, "%PROCESS_BODY%", body.str());
    s = tpl_replace(s, "%OPTIONAL_FEATURES%", kOptionalFeaturesComment);
    s = tpl_replace(s, "%SEMANTIC_TAG_EXAMPLE_PARAM%", effective_params[0].name);
    s = tpl_replace(s, "%SEMANTIC_TAG_EXAMPLE_TAG%", "probability_01");
    return s;
}

static std::string audio_template(const std::string& name, const std::string& struct_name,
                                   const std::vector<VividPortSpec>& ports,
                                   const std::vector<VividParamSpec>& params) {
    bool custom_ports  = !ports.empty();
    bool custom_params = !params.empty();

    std::vector<VividPortSpec> effective_ports;
    if (custom_ports) {
        effective_ports = ports;
    } else {
        effective_ports.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT});
        effective_ports.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
    }

    std::vector<VividParamSpec> effective_params;
    if (custom_params) {
        effective_params = params;
    } else {
        effective_params.push_back({"gain", VIVID_PARAM_FLOAT, 1.0f, 0.0f, 2.0f});
    }

    int num_input_bufs  = 0;
    int num_output_bufs = 0;
    for (const auto& p : effective_ports) {
        if (p.type == VIVID_PORT_AUDIO_BUFFER) {
            if (p.direction == VIVID_PORT_INPUT) ++num_input_bufs;
            else ++num_output_bufs;
        }
    }

    // Build process body
    std::ostringstream body;
    if (num_input_bufs > 0)
        body << "        float* in  = ctx->input_buffers[0];\n";
    if (num_output_bufs > 0)
        body << "        float* out = ctx->output_buffers[0];\n";
    for (int i = 1; i < num_output_bufs; ++i)
        body << "        float* out" << i << " = ctx->output_buffers[" << i << "];  // TODO\n";

    if (!custom_params && num_input_bufs > 0 && num_output_bufs > 0) {
        body << "        float g = gain.value;\n\n";
        body << "        for (uint32_t i = 0; i < ctx->buffer_size; i++)\n";
        body << "            out[i] = in[i] * g;\n";
    } else if (num_input_bufs > 0 && num_output_bufs > 0) {
        body << "\n        for (uint32_t i = 0; i < ctx->buffer_size; i++)\n";
        body << "            out[i] = in[i];\n";
    } else if (num_output_bufs > 0) {
        body << "\n        for (uint32_t i = 0; i < ctx->buffer_size; i++)\n";
        body << "            out[i] = 0.0f;  // TODO\n";
    }
    for (int i = 1; i < num_output_bufs; ++i)
        body << "        // TODO: fill out" << i << "\n";

    std::string s = kAudioTemplate;
    s = tpl_replace(s, "%STRUCT%", struct_name);
    s = tpl_replace(s, "%CUSTOM_TYPE_SUPPORT%", build_custom_type_support(effective_ports));
    s = tpl_replace(s, "%PARAM_DECLS%", build_param_decls(effective_params));
    s = tpl_replace(s, "%PARAMS_MACRO%", build_params_macro(effective_params));
    s = tpl_replace(s, "%PORTS_MACRO%", build_ports_macro(effective_ports));
    s = tpl_replace(s, "%PROCESS_BODY%", body.str());
    s = tpl_replace(s, "%OPTIONAL_FEATURES%", kOptionalFeaturesComment);
    s = tpl_replace(s, "%SEMANTIC_TAG_EXAMPLE_PARAM%", effective_params[0].name);
    s = tpl_replace(s, "%SEMANTIC_TAG_EXAMPLE_TAG%", "amplitude_linear");
    return s;
}

static std::string gpu_template(const std::string& name, const std::string& struct_name,
                                 const std::vector<VividPortSpec>& ports,
                                 const std::vector<VividParamSpec>& params) {
    bool custom_ports  = !ports.empty();
    bool custom_params = !params.empty();

    std::vector<VividPortSpec> effective_ports;
    if (custom_ports) {
        effective_ports = ports;
    } else {
        effective_ports.push_back({"input",   VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        effective_ports.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    std::vector<VividParamSpec> effective_params;
    if (custom_params) {
        effective_params = params;
    } else {
        effective_params.push_back({"amount", VIVID_PARAM_FLOAT, 1.0f, 0.0f, 1.0f});
    }

    int num_extra_outputs = count_ports(effective_ports, VIVID_PORT_OUTPUT) - 1;
    std::string extra_outputs;
    if (num_extra_outputs > 0) {
        extra_outputs = "    // Extra output textures are available via g->aux_output_texture_views[0.."
            + std::to_string(num_extra_outputs - 1) + "] in a custom process() override.\n";
    }

    std::string s = kGpuTemplate;
    s = tpl_replace(s, "%STRUCT%", struct_name);
    s = tpl_replace(s, "%NAME%", name);
    s = tpl_replace(s, "%CUSTOM_TYPE_SUPPORT%", build_custom_type_support(effective_ports));
    s = tpl_replace(s, "%PARAM_DECLS%", build_param_decls(effective_params));
    s = tpl_replace(s, "%PARAMS_MACRO%", build_params_macro(effective_params));
    s = tpl_replace(s, "%PORTS_MACRO%", build_ports_macro(effective_ports));
    s = tpl_replace(s, "%EXTRA_OUTPUTS%", extra_outputs);
    s = tpl_replace(s, "%OPTIONAL_FEATURES%", kOptionalFeaturesComment);
    s = tpl_replace(s, "%SEMANTIC_TAG_EXAMPLE_PARAM%", effective_params[0].name);
    s = tpl_replace(s, "%SEMANTIC_TAG_EXAMPLE_TAG%", "probability_01");
    return s;
}

static std::string child_op_control_template(const std::string& name, const std::string& struct_name) {
    std::string s = kChildOpTemplate;
    s = tpl_replace(s, "%STRUCT%", struct_name);
    return s;
}

// The empty variant has no default Param — the semantic-tag example still
// shows up as a guidance comment, but references a placeholder name the
// author will replace.
static std::string empty_control_template(const std::string& struct_name) {
    std::string s = kEmptyControlTemplate;
    s = tpl_replace(s, "%STRUCT%", struct_name);
    s = tpl_replace(s, "%OPTIONAL_FEATURES%", kOptionalFeaturesComment);
    s = tpl_replace(s, "%SEMANTIC_TAG_EXAMPLE_PARAM%", "my_param");
    s = tpl_replace(s, "%SEMANTIC_TAG_EXAMPLE_TAG%", "probability_01");
    return s;
}

static std::string empty_audio_template(const std::string& struct_name) {
    std::string s = kEmptyAudioTemplate;
    s = tpl_replace(s, "%STRUCT%", struct_name);
    s = tpl_replace(s, "%OPTIONAL_FEATURES%", kOptionalFeaturesComment);
    s = tpl_replace(s, "%SEMANTIC_TAG_EXAMPLE_PARAM%", "gain");
    s = tpl_replace(s, "%SEMANTIC_TAG_EXAMPLE_TAG%", "amplitude_linear");
    return s;
}

static std::string empty_gpu_template(const std::string& name, const std::string& struct_name) {
    std::string s = kEmptyGpuTemplate;
    s = tpl_replace(s, "%STRUCT%", struct_name);
    s = tpl_replace(s, "%NAME%", name);
    s = tpl_replace(s, "%OPTIONAL_FEATURES%", kOptionalFeaturesComment);
    s = tpl_replace(s, "%SEMANTIC_TAG_EXAMPLE_PARAM%", "my_param");
    s = tpl_replace(s, "%SEMANTIC_TAG_EXAMPLE_TAG%", "probability_01");
    return s;
}

static std::string gpu_shader_template() {
    return kGpuShaderTemplate;
}

// ---------------------------------------------------------------------------
// CMakeLists.txt patching
// ---------------------------------------------------------------------------

// Insertion markers: each kind's operators are added before the next section's comment.
// Control → before "# --- GPU operator plugins"
// GPU     → before "# --- SyphonOut operator"
// Audio   → before "# --- Operators meta-target"

static std::string cmake_insertion_marker(VividOperatorKind kind) {
    switch (kind) {
        case VIVID_OP_CONTROL: return "# --- GPU operator plugins";
        case VIVID_OP_GPU:   return "# --- SyphonOut operator";
        case VIVID_OP_AUDIO: return "# --- Operators meta-target";
        default: return "";
    }
}

static bool patch_cmake(const std::string& src_dir, const std::string& name,
                         VividOperatorKind kind, const std::string& extra_libs,
                         std::string& error) {
    std::string cmake_path = src_dir + "/CMakeLists.txt";
    std::ifstream ifs(cmake_path);
    if (!ifs) {
        error = "cannot open CMakeLists.txt at " + cmake_path;
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    ifs.close();

    std::string marker = cmake_insertion_marker(kind);
    auto pos = content.find(marker);
    if (pos == std::string::npos) {
        error = "cannot find insertion marker '" + marker + "' in CMakeLists.txt";
        return false;
    }

    const char* dsub = kind_subdir(kind);
    std::ostringstream block;
    block << "add_vivid_operator(" << name << std::string(std::max(0, 16 - static_cast<int>(name.size())), ' ')
          << " operators/" << dsub << "/" << name << "/" << name << ".cpp";
    if (kind == VIVID_OP_GPU)
        block << "  EXTRA_LIBS webgpu";
    else if (!extra_libs.empty())
        block << "  EXTRA_LIBS " << extra_libs;
    block << ")\n";

    content.insert(pos, block.str() + "\n");

    std::ofstream ofs(cmake_path);
    if (!ofs) {
        error = "cannot write CMakeLists.txt";
        return false;
    }
    ofs << content;
    return true;
}

static bool patch_package_cmake(const std::string& pkg_dir, const std::string& name,
                                std::string& error) {
    std::string cmake_path = pkg_dir + "/CMakeLists.txt";
    std::ifstream ifs(cmake_path);
    if (!ifs) {
        error = "cannot open CMakeLists.txt at " + cmake_path;
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    ifs.close();

    if (content.find(name) != std::string::npos) return true;

    size_t scan = 0;
    while (true) {
        size_t set_pos = content.find("set(", scan);
        if (set_pos == std::string::npos) break;
        size_t close = content.find(')', set_pos);
        if (close == std::string::npos) break;
        std::string block = content.substr(set_pos, close - set_pos + 1);
        if (block.find("_OPS") != std::string::npos) {
            content.insert(close, "\n  " + name);
            std::ofstream ofs(cmake_path);
            if (!ofs) {
                error = "cannot write CMakeLists.txt";
                return false;
            }
            ofs << content;
            return true;
        }
        scan = close + 1;
    }

    error = "cannot find package ops list (set(..._OPS ...)) in CMakeLists.txt";
    return false;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string OperatorCreator::validate_name(const std::string& name, const OperatorRegistry& reg) {
    if (!is_valid_identifier(name))
        return "name must be lowercase_with_underscores, start with a letter, "
               "no double underscores (got '" + name + "')";

    // Check collision with existing operator types
    std::string pascal = to_pascal_case(name);
    auto types = reg.type_names();
    for (const auto& t : types) {
        if (t == pascal)
            return "operator '" + pascal + "' already exists";
    }

    return {};  // valid
}

CreateOperatorResult OperatorCreator::create(const VividCreateOperatorRequest& request,
                                              const std::string& src_dir,
                                              bool package_layout) {
    CreateOperatorResult result{};
    const char* dsub = kind_subdir(request.kind);
    if (!dsub) {
        result.error = "invalid operator kind";
        return result;
    }

    // Validate variant
    if (!request.variant.empty() && request.variant != "child_op" && request.variant != "empty") {
        result.error = "unknown variant '" + request.variant + "' (supported: child_op, empty)";
        return result;
    }
    if (request.variant == "child_op" && request.kind != VIVID_OP_CONTROL) {
        result.error = "child_op variant is only supported for control operators";
        return result;
    }

    // Validate port names: no duplicates, no empty
    {
        std::vector<std::string> port_names;
        std::vector<VividPortSpec> custom_ports;
        for (const auto& p : request.ports) {
            if (p.name.empty()) {
                result.error = "port names must not be empty";
                return result;
            }
            if (is_custom_port_spec(p)) {
                if (p.type_name.empty()) {
                    result.error = "custom port '" + p.name + "' must define type_name";
                    return result;
                }
                if (!is_valid_cpp_type_name(p.type_name)) {
                    result.error = "custom port '" + p.name + "' must define a valid C++ type_name";
                    return result;
                }
                if (p.stable_type_id.empty()) {
                    result.error = "custom port '" + p.name + "' must define stable_type_id";
                    return result;
                }
                if (!is_valid_stable_type_id_literal(p.stable_type_id)) {
                    result.error = "custom port '" + p.name + "' must use a lowercase namespaced stable_type_id";
                    return result;
                }
                if (p.transport != VIVID_PORT_TRANSPORT_CUSTOM_REF &&
                    p.transport != VIVID_PORT_TRANSPORT_CUSTOM_VALUE) {
                    result.error = "custom port '" + p.name + "' must use CUSTOM_REF or CUSTOM_VALUE transport";
                    return result;
                }
                if (p.payload_size == 0) {
                    result.error = "custom port '" + p.name + "' must define payload_size > 0";
                    return result;
                }
                for (const auto& existing : custom_ports) {
                    if (existing.stable_type_id == p.stable_type_id &&
                        (existing.type_name != p.type_name ||
                         existing.transport != p.transport ||
                         existing.payload_size != p.payload_size ||
                         existing.audio_safe != p.audio_safe)) {
                        result.error = "custom ports with stable_type_id '" + p.stable_type_id +
                                       "' must agree on type_name, transport, payload_size, and audio_safe";
                        return result;
                    }
                    if (existing.type_name == p.type_name &&
                        (existing.stable_type_id != p.stable_type_id ||
                         existing.transport != p.transport ||
                         existing.payload_size != p.payload_size ||
                         existing.audio_safe != p.audio_safe)) {
                        result.error = "custom ports with type_name '" + p.type_name +
                                       "' must agree on stable_type_id, transport, payload_size, and audio_safe";
                        return result;
                    }
                }
                custom_ports.push_back(p);
            } else if (has_custom_port_fields(p)) {
                result.error = "built-in port '" + p.name + "' must not define custom port metadata";
                return result;
            }
            for (const auto& existing : port_names) {
                if (existing == p.name) {
                    result.error = "duplicate port name '" + p.name + "'";
                    return result;
                }
            }
            port_names.push_back(p.name);
        }
    }

    std::string struct_name = to_pascal_case(request.name);
    std::string op_dir;
    std::string cpp_path;
    if (package_layout) {
        op_dir = src_dir + "/src";
        cpp_path = op_dir + "/" + request.name + ".cpp";
    } else {
        op_dir = src_dir + "/operators/" + dsub + "/" + request.name;
        cpp_path = op_dir + "/" + request.name + ".cpp";
    }

    // Check filesystem collision
    if (package_layout ? fs::exists(cpp_path) : fs::exists(op_dir)) {
        result.error = package_layout
            ? ("file already exists: " + cpp_path)
            : ("directory already exists: " + op_dir);
        return result;
    }

    // Create directory
    std::error_code ec;
    fs::create_directories(op_dir, ec);
    if (ec) {
        result.error = "cannot create directory: " + ec.message();
        return result;
    }

    // Generate source from template
    std::string source;
    if (request.variant == "empty") {
        switch (request.kind) {
            case VIVID_OP_CONTROL: source = empty_control_template(struct_name); break;
            case VIVID_OP_AUDIO: source = empty_audio_template(struct_name);   break;
            case VIVID_OP_GPU:   source = empty_gpu_template(request.name, struct_name); break;
            default: break;
        }
    } else if (request.variant == "child_op") {
        source = child_op_control_template(request.name, struct_name);
    } else {
        switch (request.kind) {
            case VIVID_OP_CONTROL: source = control_template(request.name, struct_name, request.ports, request.params); break;
            case VIVID_OP_AUDIO: source = audio_template(request.name, struct_name, request.ports, request.params);   break;
            case VIVID_OP_GPU:   source = gpu_template(request.name, struct_name, request.ports, request.params);     break;
            default: break;
        }
    }

    {
        std::ofstream ofs(cpp_path);
        if (!ofs) {
            result.error = "cannot write " + cpp_path;
            return result;
        }
        ofs << source;
    }

    // GPU operators also need a .wgsl shader file
    if (request.kind == VIVID_OP_GPU) {
        std::string wgsl_path = op_dir + "/" + request.name + ".wgsl";
        std::ofstream ofs(wgsl_path);
        if (!ofs) {
            result.error = "cannot write " + wgsl_path;
            return result;
        }
        ofs << gpu_shader_template();
    }

    // Patch CMakeLists.txt
    std::string extra_libs;
    if (request.variant == "child_op")
        extra_libs = "vivid_embeddable_op_support";

    std::string cmake_err;
    bool patch_ok = false;
    if (package_layout)
        patch_ok = patch_package_cmake(src_dir, request.name, cmake_err);
    else
        patch_ok = patch_cmake(src_dir, request.name, request.kind, extra_libs, cmake_err);
    if (!patch_ok) {
        result.error = cmake_err;
        return result;
    }

    result.success = true;
    result.cpp_path = cpp_path;
    result.target_name = request.name;
    return result;
}

void OperatorCreator::open_in_editor(const std::string& path) {
    // Try $VISUAL, then $EDITOR, then macOS `open`
    const char* editor = std::getenv("VISUAL");
    if (!editor) editor = std::getenv("EDITOR");

    if (editor) {
        // $EDITOR/$VISUAL may be multi-word (e.g. "code --wait"), so we
        // run it through the shell. This is the one intentional shell escape.
        std::string cmd = std::string(editor) + " '" + path + "' &";
        std::fprintf(stderr, "[vivid] Opening in editor: %s\n", cmd.c_str());
        spawn_detached({"/bin/sh", "-c", cmd});
    } else {
        // macOS fallback — no shell needed.
        std::fprintf(stderr, "[vivid] Opening in editor: open %s\n", path.c_str());
        spawn_detached({"/usr/bin/open", path});
    }
}

} // namespace vivid
