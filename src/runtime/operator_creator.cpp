#include "runtime/operator_creator.h"
#include "runtime/operator_registry.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

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

static const char* port_type_name(VividPortType t) {
    switch (t) {
        case VIVID_PORT_CONTROL_FLOAT:        return "VIVID_PORT_CONTROL_FLOAT";
        case VIVID_PORT_CONTROL_INT:          return "VIVID_PORT_CONTROL_INT";
        case VIVID_PORT_CONTROL_BOOL:         return "VIVID_PORT_CONTROL_BOOL";
        case VIVID_PORT_AUDIO_FLOAT:          return "VIVID_PORT_AUDIO_FLOAT";
        case VIVID_PORT_CONTROL_SPREAD:       return "VIVID_PORT_CONTROL_SPREAD";
        case VIVID_PORT_GPU_TEXTURE:          return "VIVID_PORT_GPU_TEXTURE";
        case VIVID_PORT_DATA:                 return "VIVID_PORT_DATA";
        case VIVID_PORT_MEDIA_STREAM:         return "VIVID_PORT_MEDIA_STREAM";
        case VIVID_PORT_MEDIA_CLOCK:          return "VIVID_PORT_MEDIA_CLOCK";
        case VIVID_PORT_MIDI:                 return "VIVID_PORT_MIDI";
        case VIVID_PORT_CONTROL_STRING:       return "VIVID_PORT_CONTROL_STRING";
        case VIVID_PORT_CONTROL_STRING_SPREAD:return "VIVID_PORT_CONTROL_STRING_SPREAD";
        default:                              return "VIVID_PORT_CONTROL_FLOAT";
    }
}

static const char* domain_subdir(VividDomain d) {
    switch (d) {
        case VIVID_DOMAIN_CONTROL: return "control";
        case VIVID_DOMAIN_AUDIO:   return "audio";
        case VIVID_DOMAIN_GPU:     return "gpu";
        default: return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------

static std::string control_template(const std::string& name, const std::string& struct_name,
                                     const std::vector<OutputPortSpec>& extra_outputs) {
    std::ostringstream s;
    s << "#include \"operator_api/operator.h\"\n\n";
    s << "struct " << struct_name << " : vivid::ControlOperatorBase {\n";
    s << "    static constexpr const char* kName   = \"" << struct_name << "\";\n";
    s << "    static constexpr bool kTimeDependent = false;\n\n";
    s << "    vivid::Param<float> amount{\"amount\", 1.0f, 0.0f, 1.0f};\n\n";
    s << "    " << struct_name << "() {\n";
    s << "        // Semantic metadata is optional but recommended for MCP/LLM workflows.\n";
    s << "        vivid::semantic_tag(amount, \"probability_01\");\n";
    s << "        vivid::semantic_shape(amount, \"scalar\");\n";
    s << "    }\n\n";
    s << "    void collect_params(std::vector<vivid::ParamBase*>& out) override {\n";
    s << "        out.push_back(&amount);\n";
    s << "    }\n\n";
    s << "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n";
    s << "        out.push_back({\"input\",  VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});\n";
    s << "        out.push_back({\"output\", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});\n";
    for (const auto& ep : extra_outputs)
        s << "        out.push_back({\"" << ep.name << "\", " << port_type_name(ep.type) << ", VIVID_PORT_OUTPUT});\n";
    s << "    }\n\n";
    s << "    void process(const VividProcessContext* ctx) override {\n";
    s << "        ctx->output_values[0] = ctx->input_values[0] * amount.value;\n";
    for (size_t i = 0; i < extra_outputs.size(); ++i)
        s << "        ctx->output_values[" << (i + 1) << "] = 0.0f;  // TODO: fill " << extra_outputs[i].name << "\n";
    s << "    }\n";
    s << "};\n\n";
    s << "VIVID_REGISTER(" << struct_name << ")\n";
    return s.str();
}

static std::string audio_template(const std::string& name, const std::string& struct_name,
                                   const std::vector<OutputPortSpec>& extra_outputs) {
    std::ostringstream s;
    s << "#include \"operator_api/operator.h\"\n\n";
    s << "struct " << struct_name << " : vivid::AudioOperatorBase {\n";
    s << "    static constexpr const char* kName   = \"" << struct_name << "\";\n";
    s << "    static constexpr bool kTimeDependent = true;\n\n";
    s << "    vivid::Param<float> gain{\"gain\", 1.0f, 0.0f, 2.0f};\n\n";
    s << "    " << struct_name << "() {\n";
    s << "        // Semantic metadata is optional but recommended for MCP/LLM workflows.\n";
    s << "        vivid::semantic_tag(gain, \"amplitude_linear\");\n";
    s << "        vivid::semantic_shape(gain, \"scalar\");\n";
    s << "    }\n\n";
    s << "    void collect_params(std::vector<vivid::ParamBase*>& out) override {\n";
    s << "        out.push_back(&gain);\n";
    s << "    }\n\n";
    s << "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n";
    s << "        out.push_back({\"input\",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT});\n";
    s << "        out.push_back({\"output\", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});\n";
    for (const auto& ep : extra_outputs)
        s << "        out.push_back({\"" << ep.name << "\", " << port_type_name(ep.type) << ", VIVID_PORT_OUTPUT});\n";
    s << "    }\n\n";
    s << "    void process_audio(const VividAudioContext* ctx) override {\n";
    s << "        float* in  = ctx->input_buffers[0];\n";
    s << "        float* out = ctx->output_buffers[0];\n";
    for (size_t i = 0; i < extra_outputs.size(); ++i)
        s << "        float* out" << (i + 1) << " = ctx->output_buffers[" << (i + 1) << "];  // " << extra_outputs[i].name << "\n";
    s << "        float g = gain.value;\n\n";
    s << "        for (uint32_t i = 0; i < ctx->buffer_size; i++)\n";
    s << "            out[i] = in[i] * g;\n";
    for (size_t i = 0; i < extra_outputs.size(); ++i)
        s << "        // TODO: fill out" << (i + 1) << " (" << extra_outputs[i].name << ")\n";
    s << "    }\n";
    s << "};\n\n";
    s << "VIVID_REGISTER(" << struct_name << ")\n";
    return s.str();
}

static std::string gpu_template(const std::string& name, const std::string& struct_name,
                                 const std::vector<OutputPortSpec>& extra_outputs) {
    std::ostringstream s;
    s << "#include \"operator_api/wgsl_filter.h\"\n\n";
    s << "struct " << struct_name << " : vivid::WgslFilterBase {\n";
    s << "    static constexpr const char* kName   = \"" << struct_name << "\";\n";
    s << "    static constexpr bool kTimeDependent = true;\n\n";
    s << "    vivid::Param<float> amount{\"amount\", 1.0f, 0.0f, 1.0f};\n\n";
    s << "    " << struct_name << "() : WgslFilterBase(\"" << name << ".wgsl\") {\n";
    s << "        // Semantic metadata is optional but recommended for MCP/LLM workflows.\n";
    s << "        vivid::semantic_tag(amount, \"probability_01\");\n";
    s << "        vivid::semantic_shape(amount, \"scalar\");\n";
    s << "    }\n\n";
    s << "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n";
    s << "        out.push_back({\"input\",   VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});\n";
    s << "        out.push_back({\"texture\", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});\n";
    for (const auto& ep : extra_outputs)
        s << "        out.push_back({\"" << ep.name << "\", " << port_type_name(ep.type) << ", VIVID_PORT_OUTPUT});\n";
    s << "    }\n\n";
    s << "    void collect_params(std::vector<vivid::ParamBase*>& out) override {\n";
    s << "        out.push_back(&amount);\n";
    s << "    }\n";
    if (!extra_outputs.empty()) {
        s << "\n    // Extra output textures are available via g->aux_output_texture_views[0.."
          << (extra_outputs.size() - 1) << "] in a custom process() override.\n";
    }
    s << "};\n\n";
    s << "VIVID_REGISTER(" << struct_name << ")\n";
    return s.str();
}

static std::string composite_control_template(const std::string& name, const std::string& struct_name) {
    std::ostringstream s;
    s << "#include \"operator_api/child_op.h\"\n";
    s << "#include \"control/lfo/lfo.h\"\n";
    s << "#include \"control/smooth/smooth.h\"\n\n";
    s << "struct " << struct_name << " : vivid::ControlOperatorBase {\n";
    s << "    static constexpr const char* kName   = \"" << struct_name << "\";\n";
    s << "    static constexpr bool kTimeDependent = true;\n\n";
    s << "    vivid::Param<float> amount   {\"amount\",    1.0f, 0.0f, 10.0f};\n";
    s << "    vivid::Param<float> lfo_rate {\"lfo_rate\",  2.0f, 0.01f, 20.0f};\n";
    s << "    vivid::Param<float> lfo_depth{\"lfo_depth\", 0.5f, 0.0f, 1.0f};\n";
    s << "    vivid::Param<float> smooth_time{\"smooth_time\", 0.05f, 0.0f, 2.0f};\n\n";
    s << "    vivid::ChildOp<LFO>    lfo_;\n";
    s << "    vivid::ChildOp<Smooth> smoother_;\n\n";
    s << "    " << struct_name << "() {\n";
    s << "        // Semantic metadata is optional but recommended for MCP/LLM workflows.\n";
    s << "        vivid::semantic_tag(amount, \"amplitude_linear\");\n";
    s << "        vivid::semantic_shape(amount, \"scalar\");\n";
    s << "        vivid::semantic_tag(lfo_rate, \"frequency_hz\");\n";
    s << "        vivid::semantic_shape(lfo_rate, \"scalar\");\n";
    s << "        vivid::semantic_unit(lfo_rate, \"Hz\");\n";
    s << "        vivid::semantic_tag(lfo_depth, \"amplitude_linear\");\n";
    s << "        vivid::semantic_shape(lfo_depth, \"scalar\");\n";
    s << "        vivid::semantic_tag(smooth_time, \"time_seconds\");\n";
    s << "        vivid::semantic_shape(smooth_time, \"scalar\");\n";
    s << "        vivid::semantic_unit(smooth_time, \"s\");\n";
    s << "    }\n\n";
    s << "    void collect_params(std::vector<vivid::ParamBase*>& out) override {\n";
    s << "        out.push_back(&amount);\n";
    s << "        out.push_back(&lfo_rate);\n";
    s << "        out.push_back(&lfo_depth);\n";
    s << "        out.push_back(&smooth_time);\n";
    s << "    }\n\n";
    s << "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n";
    s << "        out.push_back({\"input\",  VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});\n";
    s << "        out.push_back({\"output\", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});\n";
    s << "    }\n\n";
    s << "    void process(const VividProcessContext* ctx) override {\n";
    s << "        float input = ctx->input_values[0];\n\n";
    s << "        // Drive internal LFO\n";
    s << "        lfo_.set_param(\"frequency\", lfo_rate.value);\n";
    s << "        lfo_.set_param(\"amplitude\", 1.0f);\n";
    s << "        lfo_.process(ctx);\n\n";
    s << "        // Smooth the LFO output\n";
    s << "        smoother_.set_param(\"rise_time\", smooth_time.value);\n";
    s << "        smoother_.set_param(\"fall_time\", smooth_time.value);\n";
    s << "        smoother_.set_input(\"input\", lfo_.output(\"value\"));\n";
    s << "        smoother_.process(ctx);\n\n";
    s << "        float mod = smoother_.output(\"value\");\n";
    s << "        ctx->output_values[0] = input * (amount.value + lfo_depth.value * mod);\n";
    s << "    }\n";
    s << "};\n\n";
    s << "VIVID_REGISTER(" << struct_name << ")\n";
    return s.str();
}

static std::string gpu_shader_template() {
    return "@fragment\n"
           "fn fs_main(input: VertexOutput) -> @location(0) vec4f {\n"
           "    let color = textureSample(inputTex, texSampler, input.uv);\n"
           "    return mix(color, vec4f(color.rgb * u.amount, color.a), u.amount);\n"
           "}\n";
}

// ---------------------------------------------------------------------------
// CMakeLists.txt patching
// ---------------------------------------------------------------------------

// Insertion markers: each domain's operators are added before the next section's comment.
// Control → before "# --- GPU operator plugins ---"
// GPU     → before "# --- Movie File In"
// Audio   → before "# --- Movie File Audio In"

static std::string cmake_insertion_marker(VividDomain domain) {
    switch (domain) {
        case VIVID_DOMAIN_CONTROL: return "# --- GPU operator plugins ---";
        case VIVID_DOMAIN_GPU:     return "# --- Movie File In";
        case VIVID_DOMAIN_AUDIO:   return "# --- Movie File Audio In";
        default: return "";
    }
}

static bool patch_cmake(const std::string& src_dir, const std::string& name,
                         VividDomain domain, const std::string& extra_libs,
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

    std::string marker = cmake_insertion_marker(domain);
    auto pos = content.find(marker);
    if (pos == std::string::npos) {
        error = "cannot find insertion marker '" + marker + "' in CMakeLists.txt";
        return false;
    }

    const char* dsub = domain_subdir(domain);
    std::ostringstream block;
    block << "add_vivid_operator(" << name << std::string(std::max(0, 16 - static_cast<int>(name.size())), ' ')
          << " operators/" << dsub << "/" << name << "/" << name << ".cpp";
    if (domain == VIVID_DOMAIN_GPU)
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

CreateOperatorResult OperatorCreator::create(const std::string& name, VividDomain domain,
                                              const std::string& src_dir,
                                              const std::string& variant,
                                              bool package_layout,
                                              const std::vector<OutputPortSpec>& extra_outputs) {
    CreateOperatorResult result{};
    const char* dsub = domain_subdir(domain);
    if (!dsub) {
        result.error = "invalid domain";
        return result;
    }

    // Validate variant
    if (!variant.empty() && variant != "composite") {
        result.error = "unknown variant '" + variant + "' (supported: composite)";
        return result;
    }
    if (variant == "composite" && domain != VIVID_DOMAIN_CONTROL) {
        result.error = "composite variant is only supported for control domain";
        return result;
    }

    std::string struct_name = to_pascal_case(name);
    std::string op_dir;
    std::string cpp_path;
    if (package_layout) {
        op_dir = src_dir + "/src";
        cpp_path = op_dir + "/" + name + ".cpp";
    } else {
        op_dir = src_dir + "/operators/" + dsub + "/" + name;
        cpp_path = op_dir + "/" + name + ".cpp";
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
    if (variant == "composite") {
        source = composite_control_template(name, struct_name);
    } else {
        switch (domain) {
            case VIVID_DOMAIN_CONTROL: source = control_template(name, struct_name, extra_outputs); break;
            case VIVID_DOMAIN_AUDIO:   source = audio_template(name, struct_name, extra_outputs);   break;
            case VIVID_DOMAIN_GPU:     source = gpu_template(name, struct_name, extra_outputs);     break;
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
    if (domain == VIVID_DOMAIN_GPU) {
        std::string wgsl_path = op_dir + "/" + name + ".wgsl";
        std::ofstream ofs(wgsl_path);
        if (!ofs) {
            result.error = "cannot write " + wgsl_path;
            return result;
        }
        ofs << gpu_shader_template();
    }

    // Patch CMakeLists.txt
    std::string extra_libs;
    if (variant == "composite")
        extra_libs = "vivid_composable_ops";

    std::string cmake_err;
    bool patch_ok = false;
    if (package_layout)
        patch_ok = patch_package_cmake(src_dir, name, cmake_err);
    else
        patch_ok = patch_cmake(src_dir, name, domain, extra_libs, cmake_err);
    if (!patch_ok) {
        result.error = cmake_err;
        return result;
    }

    result.success = true;
    result.cpp_path = cpp_path;
    result.target_name = name;
    return result;
}

void OperatorCreator::open_in_editor(const std::string& path) {
    // Try $VISUAL, then $EDITOR, then macOS `open`
    const char* editor = std::getenv("VISUAL");
    if (!editor) editor = std::getenv("EDITOR");

    std::string cmd;
    if (editor) {
        cmd = std::string(editor) + " \"" + path + "\" &";
    } else {
        // macOS fallback
        cmd = "open \"" + path + "\" &";
    }

    std::fprintf(stderr, "[vivid] Opening in editor: %s\n", cmd.c_str());
    // Fire-and-forget
    (void)std::system(cmd.c_str());
}

} // namespace vivid
