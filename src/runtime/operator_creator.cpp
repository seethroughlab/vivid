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

static std::string control_template(const std::string& name, const std::string& struct_name) {
    std::ostringstream s;
    s << "#include \"operator_api/operator.h\"\n\n";
    s << "struct " << struct_name << " : vivid::OperatorBase {\n";
    s << "    static constexpr const char* kName   = \"" << struct_name << "\";\n";
    s << "    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;\n";
    s << "    static constexpr bool kTimeDependent = false;\n\n";
    s << "    vivid::Param<float> amount{\"amount\", 1.0f, 0.0f, 1.0f};\n\n";
    s << "    void collect_params(std::vector<vivid::ParamBase*>& out) override {\n";
    s << "        out.push_back(&amount);\n";
    s << "    }\n\n";
    s << "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n";
    s << "        out.push_back({\"input\",  VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});\n";
    s << "        out.push_back({\"output\", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});\n";
    s << "    }\n\n";
    s << "    void process(const VividProcessContext* ctx) override {\n";
    s << "        ctx->output_values[0] = ctx->input_values[0] * amount.value;\n";
    s << "    }\n";
    s << "};\n\n";
    s << "VIVID_REGISTER(" << struct_name << ")\n";
    return s.str();
}

static std::string audio_template(const std::string& name, const std::string& struct_name) {
    std::ostringstream s;
    s << "#include \"operator_api/operator.h\"\n";
    s << "#include \"operator_api/audio_operator.h\"\n\n";
    s << "struct " << struct_name << " : vivid::OperatorBase {\n";
    s << "    static constexpr const char* kName   = \"" << struct_name << "\";\n";
    s << "    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;\n";
    s << "    static constexpr bool kTimeDependent = true;\n\n";
    s << "    vivid::Param<float> gain{\"gain\", 1.0f, 0.0f, 2.0f};\n\n";
    s << "    void collect_params(std::vector<vivid::ParamBase*>& out) override {\n";
    s << "        out.push_back(&gain);\n";
    s << "    }\n\n";
    s << "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n";
    s << "        out.push_back({\"input\",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT});\n";
    s << "        out.push_back({\"output\", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});\n";
    s << "    }\n\n";
    s << "    void process(const VividProcessContext* ctx) override {\n";
    s << "        auto* audio = vivid_audio(ctx);\n";
    s << "        if (!audio) return;\n\n";
    s << "        float* in  = audio->input_buffers[0];\n";
    s << "        float* out = audio->output_buffers[0];\n";
    s << "        float g = gain.value;\n\n";
    s << "        for (uint32_t i = 0; i < audio->buffer_size; i++)\n";
    s << "            out[i] = in[i] * g;\n";
    s << "    }\n";
    s << "};\n\n";
    s << "VIVID_REGISTER(" << struct_name << ")\n";
    return s.str();
}

static std::string gpu_template(const std::string& name, const std::string& struct_name) {
    std::ostringstream s;
    s << "#include \"operator_api/wgsl_filter.h\"\n\n";
    s << "struct " << struct_name << " : vivid::WgslFilterBase {\n";
    s << "    static constexpr const char* kName   = \"" << struct_name << "\";\n";
    s << "    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;\n";
    s << "    static constexpr bool kTimeDependent = true;\n\n";
    s << "    vivid::Param<float> amount{\"amount\", 1.0f, 0.0f, 1.0f};\n\n";
    s << "    " << struct_name << "() : WgslFilterBase(\"" << name << ".wgsl\") {}\n\n";
    s << "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n";
    s << "        out.push_back({\"input\",   VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});\n";
    s << "        out.push_back({\"texture\", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});\n";
    s << "    }\n\n";
    s << "    void collect_params(std::vector<vivid::ParamBase*>& out) override {\n";
    s << "        out.push_back(&amount);\n";
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
// GPU     → before "# --- Audio operator plugins ---"
//   (but movie_file_in is a special case after normal GPU operators,
//    so GPU inserts before "# --- Movie File In")
// Audio   → before "# --- Glitch operator plugins ---"
//   (glitch operators are a subsection of audio, so new audio operators go before glitch)

static std::string cmake_insertion_marker(VividDomain domain) {
    switch (domain) {
        case VIVID_DOMAIN_CONTROL: return "# --- GPU operator plugins ---";
        case VIVID_DOMAIN_GPU:     return "# --- Movie File In";
        case VIVID_DOMAIN_AUDIO:   return "# --- Glitch operator plugins ---";
        default: return "";
    }
}

static bool patch_cmake(const std::string& src_dir, const std::string& name,
                         VividDomain domain, std::string& error) {
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
                                              const std::string& src_dir) {
    CreateOperatorResult result{};
    const char* dsub = domain_subdir(domain);
    if (!dsub) {
        result.error = "invalid domain";
        return result;
    }

    std::string struct_name = to_pascal_case(name);
    std::string op_dir = src_dir + "/operators/" + dsub + "/" + name;
    std::string cpp_path = op_dir + "/" + name + ".cpp";

    // Check filesystem collision
    if (fs::exists(op_dir)) {
        result.error = "directory already exists: " + op_dir;
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
    switch (domain) {
        case VIVID_DOMAIN_CONTROL: source = control_template(name, struct_name); break;
        case VIVID_DOMAIN_AUDIO:   source = audio_template(name, struct_name);   break;
        case VIVID_DOMAIN_GPU:     source = gpu_template(name, struct_name);     break;
        default: break;
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
    std::string cmake_err;
    if (!patch_cmake(src_dir, name, domain, cmake_err)) {
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
