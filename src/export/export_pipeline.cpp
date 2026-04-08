#include "export/export_pipeline.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/core/tool_discovery.h"
#include "runtime/graph/graph.h"
#include "operator_api/port_type_registry.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <unordered_set>
#include <algorithm>
#include "runtime/platform/process_runner.h"

namespace vivid {

static std::string cpp_string_literal(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    out += "\"";
    return out;
}

static bool str_ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

ExportPipeline::ExportPipeline(const std::string& source_dir, const std::string& build_dir)
    : source_dir_(source_dir), build_dir_(build_dir) {}

bool ExportPipeline::run(const ExportOptions& opts, OperatorRegistry& registry) {
    headless_ = opts.headless;
    control_server_ = opts.control_server;

    // Determine export directory
    export_dir_ = opts.output_dir.empty()
        ? opts.output_name + "_export"
        : opts.output_dir;

    std::fprintf(stderr, "[export] Source dir: %s\n", source_dir_.c_str());
    std::fprintf(stderr, "[export] Build dir:  %s\n", build_dir_.c_str());
    std::fprintf(stderr, "[export] Export dir: %s\n", export_dir_.c_str());

    // 1. Load manifest
    if (!load_manifest()) return false;

    // 2. Load graph and resolve operators
    Graph graph;
    if (!graph.load(opts.graph_path.c_str())) {
        std::fprintf(stderr, "[export] Failed to load graph: %s\n", opts.graph_path.c_str());
        return false;
    }

    if (!resolve_operators(graph, opts, registry)) return false;

    // 3. Create export directory structure
    std::filesystem::create_directories(export_dir_);

    // 4. Generate files
    if (!generate_static_registry()) return false;
    if (!generate_embedded_graph(opts.graph_path)) return false;
    if (!generate_embedded_shader_operators(registry)) return false;
    if (!generate_cmakelists()) return false;
    if (!copy_standalone_main()) return false;

    // 5. Build
    if (!build()) return false;

    // 6. Copy output
    const std::string final_output_path =
        opts.output_path.empty() ? opts.output_name : opts.output_path;
    if (!copy_output(final_output_path)) return false;

    std::fprintf(stderr, "[export] Success! Binary: %s\n", final_output_path.c_str());
    return true;
}

bool ExportPipeline::load_manifest() {
    std::string path = build_dir_ + "/operator_manifest.json";

    nlohmann::json root;
    try {
        std::ifstream ifs(path);
        if (!ifs) {
            std::fprintf(stderr, "[export] Failed to read manifest: %s: cannot open file\n", path.c_str());
            return false;
        }
        root = nlohmann::json::parse(ifs);
    } catch (const nlohmann::json::parse_error& e) {
        std::fprintf(stderr, "[export] Failed to read manifest: %s: %s\n", path.c_str(), e.what());
        return false;
    }

    for (auto& [target, val] : root.items()) {
        ManifestEntry entry;

        if (val.contains("sources") && val["sources"].is_array()) {
            for (auto& s : val["sources"]) {
                if (s.is_string())
                    entry.sources.push_back(s.get<std::string>());
            }
        }

        if (val.contains("extra_libs") && val["extra_libs"].is_array()) {
            for (auto& l : val["extra_libs"]) {
                if (l.is_string())
                    entry.extra_libs.push_back(l.get<std::string>());
            }
        }

        if (val.contains("frameworks") && val["frameworks"].is_array()) {
            for (auto& f : val["frameworks"]) {
                if (f.is_string())
                    entry.frameworks.push_back(f.get<std::string>());
            }
        }

        if (val.contains("objc_arc") && val["objc_arc"].is_array()) {
            for (auto& a : val["objc_arc"]) {
                if (a.is_string())
                    entry.objc_arc.push_back(a.get<std::string>());
            }
        }

        if (val.contains("include_dirs") && val["include_dirs"].is_array()) {
            for (auto& d : val["include_dirs"]) {
                if (d.is_string())
                    entry.include_dirs.push_back(d.get<std::string>());
            }
        }

        manifest_[target] = std::move(entry);
    }

    std::fprintf(stderr, "[export] Loaded manifest: %zu operators\n", manifest_.size());
    return true;
}

bool ExportPipeline::resolve_operators(const Graph& graph, const ExportOptions& opts,
                                        OperatorRegistry& registry) {
    // Collect all operator types needed by the graph
    std::unordered_set<std::string> needed_types;
    for (const auto& node : graph.nodes()) {
        needed_types.insert(node.type);
    }

    // Add extra operators requested by the user
    for (const auto& extra : opts.extra_operators) {
        needed_types.insert(extra);
    }

    // Resolve type names to cmake targets via the registry
    resolved_ops_.clear();
    required_custom_types_.clear();
    shader_operator_types_.clear();
    needs_webgpu_ = false;
    needs_rtmidi_ = false;
    std::unordered_set<uint32_t> seen_custom_type_ids;

    for (const auto& type_name : needed_types) {
        // Skip builtins (audio_out, video_out) — they're compiled into the standalone main
        if (type_name == "audio_out" || type_name == "video_out")
            continue;

        if (registry.is_shader_operator(type_name)) {
            shader_operator_types_.push_back(type_name);
            needs_webgpu_ = true;
            continue;
        }

        // Look up cmake target
        std::string target = registry.type_to_target(type_name);
        if (target.empty()) {
            std::fprintf(stderr, "[export] ERROR: cannot resolve operator type '%s' to a cmake target\n",
                         type_name.c_str());
            return false;
        }

        auto mit = manifest_.find(target);
        if (mit == manifest_.end()) {
            std::fprintf(stderr, "[export] ERROR: operator target '%s' not found in manifest\n",
                         target.c_str());
            return false;
        }

        // Check deps
        for (const auto& lib : mit->second.extra_libs) {
            if (lib == "webgpu") needs_webgpu_ = true;
            if (lib == "rtmidi") needs_rtmidi_ = true;
        }

        bool has_custom_types = false;
        if (auto* loader = registry.find(type_name)) {
            const auto* desc = loader->descriptor();
            if (desc && desc->ports) {
                for (uint32_t pi = 0; pi < desc->port_count; ++pi) {
                    if (vivid_is_custom_port_type(desc->ports[pi].type)) {
                        has_custom_types = true;
                        if (seen_custom_type_ids.insert(desc->ports[pi].type).second) {
                            VividPortTypeInfo info{};
                            if (!vivid_lookup_port_type(desc->ports[pi].type, &info)) {
                                std::fprintf(stderr,
                                             "[export] ERROR: custom port type 0x%08x for '%s' is not registered\n",
                                             desc->ports[pi].type, type_name.c_str());
                                return false;
                            }
                            required_custom_types_.push_back(info);
                        }
                    }
                }
            }
        }

        resolved_ops_.push_back({target, type_name, mit->second, has_custom_types});
        std::fprintf(stderr, "[export] Resolved: %s -> %s\n", type_name.c_str(), target.c_str());
    }

    // Sort for deterministic output
    std::sort(resolved_ops_.begin(), resolved_ops_.end(),
              [](const auto& a, const auto& b) { return a.target < b.target; });
    std::sort(shader_operator_types_.begin(), shader_operator_types_.end());

    std::fprintf(stderr, "[export] %zu operators, %zu shader operators\n",
                 resolved_ops_.size(), shader_operator_types_.size());
    return true;
}

bool ExportPipeline::generate_static_registry() {
    std::string path = export_dir_ + "/static_registry.cpp";
    std::ofstream out(path);
    if (!out) {
        std::fprintf(stderr, "[export] Failed to create %s\n", path.c_str());
        return false;
    }

    out << "// Generated by vivid export — do not edit\n";
    out << "#include \"runtime/operator_registry.h\"\n";
    out << "#include \"operator_api/types.h\"\n\n";
    out << "#include \"operator_api/port_type_registry.h\"\n\n";

    // Forward declare renamed symbols for each operator
    for (const auto& op : resolved_ops_) {
        out << "extern \"C\" const VividOperatorDescriptor* vivid_descriptor_" << op.target << "();\n";
        out << "extern \"C\" void* vivid_create_" << op.target << "();\n";
        out << "extern \"C\" void vivid_destroy_" << op.target << "(void*);\n";
        out << "extern \"C\" void vivid_process_frame_" << op.target << "(void*, const VividFrameContext*);\n";
    }

    out << "\nvoid register_static_operators(vivid::OperatorRegistry& registry) {\n";
    if (!required_custom_types_.empty()) {
        out << "    static const VividPortTypeInfo kCustomTypes[] = {\n";
        for (const auto& info : required_custom_types_) {
            out << "        { "
                << info.type_id << ", "
                << static_cast<uint32_t>(info.transport) << ", "
                << info.payload_size << ", "
                << cpp_string_literal(info.type_name ? info.type_name : "") << ", "
                << cpp_string_literal(info.stable_type_id ? info.stable_type_id : "") << ", "
                << static_cast<uint32_t>(info.audio_safe) << ", "
                << static_cast<uint32_t>(info.reserved0) << ", "
                << static_cast<uint32_t>(info.reserved1) << ", "
                << static_cast<uint32_t>(info.reserved2) << ", "
                << info.abi_version << ", ";
            if (info.package_name && info.package_name[0] != '\0')
                out << cpp_string_literal(info.package_name);
            else
                out << "nullptr";
            out << ", ";
            if (info.description && info.description[0] != '\0')
                out << cpp_string_literal(info.description);
            else
                out << "nullptr";
            out << " },\n";
        }
        out << "    };\n";
        out << "    for (const auto& info : kCustomTypes) {\n";
        out << "        vivid_register_port_type(&info);\n";
        out << "    }\n";
    }
    for (const auto& op : resolved_ops_) {
        out << "    registry.register_builtin(\"" << op.type_name << "\",\n"
            << "        vivid_descriptor_" << op.target << ",\n"
            << "        vivid_create_" << op.target << ",\n"
            << "        vivid_destroy_" << op.target << ",\n"
            << "        vivid_process_frame_" << op.target << ");\n";
    }
    out << "}\n";

    std::fprintf(stderr, "[export] Generated static_registry.cpp (%zu operators)\n",
                 resolved_ops_.size());
    return true;
}

bool ExportPipeline::generate_embedded_graph(const std::string& graph_path) {
    std::ifstream ifs(graph_path);
    if (!ifs) {
        std::fprintf(stderr, "[export] Failed to read graph: %s\n", graph_path.c_str());
        return false;
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string json = ss.str();

    std::string path = export_dir_ + "/embedded_graph.h";
    std::ofstream out(path);
    if (!out) {
        std::fprintf(stderr, "[export] Failed to create %s\n", path.c_str());
        return false;
    }

    out << "// Generated by vivid export — do not edit\n";
    out << "#pragma once\n\n";
    out << "static const char* kEmbeddedGraphJSON = R\"VIVID_GRAPH(\n";
    out << json;
    if (!json.empty() && json.back() != '\n') out << '\n';
    out << ")VIVID_GRAPH\";\n";

    std::fprintf(stderr, "[export] Generated embedded_graph.h (%zu bytes)\n", json.size());
    return true;
}

bool ExportPipeline::generate_embedded_shader_operators(OperatorRegistry& registry) {
    if (shader_operator_types_.empty()) return true;

    std::string path = export_dir_ + "/embedded_wgsl_presets.cpp";
    std::ofstream out(path);
    if (!out) {
        std::fprintf(stderr, "[export] Failed to create %s\n", path.c_str());
        return false;
    }

    out << "// Generated by vivid export — do not edit\n";
    out << "#include \"runtime/operator_registry.h\"\n";
    out << "#include <cstdio>\n";
    out << "#include <fstream>\n";
    out << "#include <filesystem>\n\n";

    // Embed each WGSL shader as a string literal
    for (const auto& name : shader_operator_types_) {
        const auto* shader_path = registry.shader_operator_source(name);
        if (!shader_path || shader_path->empty()) continue;

        std::ifstream ifs(*shader_path);
        if (!ifs) {
            std::fprintf(stderr, "[export] WARNING: cannot read WGSL shader for %s: %s\n",
                         name.c_str(), shader_path->c_str());
            continue;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string wgsl = ss.str();

        out << "static const char* kWGSL_" << name << " = R\"VIVID_WGSL(\n";
        out << wgsl;
        if (!wgsl.empty() && wgsl.back() != '\n') out << '\n';
        out << ")VIVID_WGSL\";\n\n";
    }

    // Registration function: writes temp files and scans them as shader operators.
    out << "void register_embedded_shader_operators(vivid::OperatorRegistry& registry) {\n";
    out << "    namespace fs = std::filesystem;\n";
    out << "    auto tmp_dir = fs::temp_directory_path() / \"vivid_standalone_shader_ops\";\n";
    out << "    fs::create_directories(tmp_dir);\n\n";

    for (const auto& name : shader_operator_types_) {
        out << "    {\n";
        out << "        std::string path = (tmp_dir / \"" << name << ".wgsl\").string();\n";
        out << "        { std::ofstream ofs(path); ofs << kWGSL_" << name << "; }\n";
        out << "    }\n";
    }

    out << "    if (!registry.scan_shader_operators(tmp_dir.string())) {\n";
    out << "        std::fprintf(stderr, \"[standalone] Failed to register embedded shader operators\\n\");\n";
    out << "    }\n";
    out << "}\n";

    std::fprintf(stderr, "[export] Generated embedded_wgsl_presets.cpp (%zu shader operators)\n",
                 shader_operator_types_.size());
    return true;
}

bool ExportPipeline::generate_cmakelists() {
    // Read the template
    std::string template_path = source_dir_ + "/src/export/standalone.cmake.in";
    std::ifstream tpl(template_path);
    if (!tpl) {
        std::fprintf(stderr, "[export] Failed to read CMake template: %s\n", template_path.c_str());
        return false;
    }
    std::ostringstream tpl_ss;
    tpl_ss << tpl.rdbuf();
    std::string content = tpl_ss.str();

    // Build substitution values
    auto replace = [&](const std::string& var, const std::string& val) {
        std::string token = "${" + var + "}";
        size_t pos;
        while ((pos = content.find(token)) != std::string::npos)
            content.replace(pos, token.size(), val);
    };

    replace("VIVID_SRC", source_dir_);
    replace("VIVID_BUILD", build_dir_);
    replace("STANDALONE_HEADLESS", headless_ ? "ON" : "OFF");
    replace("STANDALONE_CONTROL_SERVER", control_server_ ? "ON" : "OFF");

    const auto webgpu_prefetch = std::filesystem::path(build_dir_) / "_deps" / "webgpu-src";
    const auto ixwebsocket_prefetch = std::filesystem::path(build_dir_) / "_deps" / "ixwebsocket-src";
    replace("WEBGPU_PREFETCH_SOURCE_DIR",
            std::filesystem::exists(webgpu_prefetch) ? webgpu_prefetch.string() : "");
    replace("IXWEBSOCKET_PREFETCH_SOURCE_DIR",
            std::filesystem::exists(ixwebsocket_prefetch) ? ixwebsocket_prefetch.string() : "");

    // Operator OBJECT library blocks
    std::ostringstream op_blocks;
    std::ostringstream op_targets;
    std::unordered_set<std::string> all_frameworks;

    for (const auto& op : resolved_ops_) {
        // Sources (absolute paths)
        std::string sources_str;
        for (const auto& src : op.manifest.sources) {
            if (!sources_str.empty()) sources_str += "\n    ";
            sources_str += "${VIVID_SRC}/" + src;
        }

        op_blocks << "add_library(op_" << op.target << " OBJECT\n"
                  << "    " << sources_str << "\n)\n";
        op_blocks << "target_include_directories(op_" << op.target << " PRIVATE\n"
                  << "    ${VIVID_SRC}/src\n"
                  << "    ${VIVID_SRC}/operators\n";
        for (const auto& inc : op.manifest.include_dirs) {
            op_blocks << "    ${VIVID_SRC}/" << inc << "\n";
        }
        op_blocks << ")\n";
        op_blocks << "target_link_libraries(op_" << op.target << " PRIVATE vivid_operator_api";
        for (const auto& lib : op.manifest.extra_libs) {
            op_blocks << " " << lib;
        }
        op_blocks << ")\n";
        op_blocks << "target_compile_definitions(op_" << op.target << " PRIVATE\n"
                  << "    vivid_descriptor=vivid_descriptor_" << op.target << "\n"
                  << "    vivid_create=vivid_create_" << op.target << "\n"
                  << "    vivid_destroy=vivid_destroy_" << op.target << "\n"
                  << "    vivid_process_frame=vivid_process_frame_" << op.target << "\n"
                  << "    vivid_draw_thumbnail=vivid_draw_thumbnail_" << op.target << "\n"
                  << "    vivid_main_thread_update=vivid_main_thread_update_" << op.target << "\n"
                  << ")\n";

        // ObjC ARC flags
        for (const auto& arc_file : op.manifest.objc_arc) {
            op_blocks << "set_source_files_properties(${VIVID_SRC}/" << arc_file
                      << " PROPERTIES COMPILE_FLAGS \"-fobjc-arc\")\n";
        }

        op_blocks << "\n";

        if (!op_targets.str().empty()) op_targets << " ";
        op_targets << "$<TARGET_OBJECTS:op_" << op.target << ">";

        for (const auto& fw : op.manifest.frameworks)
            all_frameworks.insert(fw);
    }

    replace("OPERATOR_OBJECT_LIBRARIES", op_blocks.str());
    replace("OPERATOR_OBJECT_TARGETS", op_targets.str());

    // Frameworks
    std::ostringstream fw_str;
    for (const auto& fw : all_frameworks) {
        fw_str << "    \"-framework " << fw << "\"\n";
    }
    replace("OPERATOR_FRAMEWORKS", fw_str.str());

    // WGSL sources
    std::string wgsl_sources;
    if (!shader_operator_types_.empty()) {
        wgsl_sources = "${CMAKE_CURRENT_SOURCE_DIR}/embedded_wgsl_presets.cpp";
    }
    replace("WGSL_PRESET_SOURCES", wgsl_sources);

    replace("NEEDS_WEBGPU", needs_webgpu_ ? "ON" : "OFF");
    replace("NEEDS_RTMIDI", needs_rtmidi_ ? "ON" : "OFF");

    // Write output
    std::string output_path = export_dir_ + "/CMakeLists.txt";
    std::ofstream out(output_path);
    if (!out) {
        std::fprintf(stderr, "[export] Failed to write %s\n", output_path.c_str());
        return false;
    }
    out << content;

    std::fprintf(stderr, "[export] Generated CMakeLists.txt\n");
    return true;
}

bool ExportPipeline::copy_standalone_main() {
    std::string src = source_dir_ + "/src/export/standalone_main.cpp";
    std::string dst = export_dir_ + "/standalone_main.cpp";

    std::ifstream ifs(src);
    if (!ifs) {
        std::fprintf(stderr, "[export] Failed to read %s\n", src.c_str());
        return false;
    }

    std::ofstream ofs(dst);
    if (!ofs) {
        std::fprintf(stderr, "[export] Failed to write %s\n", dst.c_str());
        return false;
    }

    ofs << ifs.rdbuf();
    std::fprintf(stderr, "[export] Copied standalone_main.cpp\n");
    return true;
}

bool ExportPipeline::build() {
    std::string cmake_exe = find_tool("cmake");
    if (cmake_exe.empty()) {
        std::fprintf(stderr, "[export] %s\n", missing_tool_error("cmake").c_str());
        return false;
    }

    std::string build_path = export_dir_ + "/build";

    // Configure
    ProcessRunOptions configure_opts;
    configure_opts.argv = {cmake_exe, "-S", export_dir_, "-B", build_path, "-DCMAKE_BUILD_TYPE=Release"};
    std::fprintf(stderr, "[export] Configuring: %s -S %s -B %s\n", cmake_exe.c_str(), export_dir_.c_str(), build_path.c_str());
    auto configure_result = run_process(configure_opts);
    if (!configure_result.launched || configure_result.exit_code != 0) {
        std::fprintf(stderr, "[export] CMake configure failed (exit %d)\n%s",
                     configure_result.exit_code, configure_result.output.c_str());
        return false;
    }

    // Build
    ProcessRunOptions build_opts;
    build_opts.argv = {cmake_exe, "--build", build_path, "--config", "Release"};
    std::fprintf(stderr, "[export] Building: %s --build %s\n", cmake_exe.c_str(), build_path.c_str());
    auto build_result = run_process(build_opts);
    if (!build_result.launched || build_result.exit_code != 0) {
        std::fprintf(stderr, "[export] Build failed (exit %d)\n%s",
                     build_result.exit_code, build_result.output.c_str());
        return false;
    }

    return true;
}

bool ExportPipeline::copy_output(const std::string& output_name) {
    std::string build_path = export_dir_ + "/build";

    // Find the built binary
    std::string binary_name = "standalone";  // the CMake target name
    std::string src_binary = build_path + "/" + binary_name;

    if (!std::filesystem::exists(src_binary)) {
        std::fprintf(stderr, "[export] Built binary not found: %s\n", src_binary.c_str());
        return false;
    }

    auto output_path = std::filesystem::path(output_name);
    if (output_path.has_parent_path()) {
        std::error_code mk_ec;
        std::filesystem::create_directories(output_path.parent_path(), mk_ec);
        if (mk_ec) {
            std::fprintf(stderr, "[export] Failed to create output directory: %s\n",
                         mk_ec.message().c_str());
            return false;
        }
    }

    // Copy binary
    std::error_code ec;
    std::filesystem::copy(src_binary, output_path,
                          std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fprintf(stderr, "[export] Failed to copy binary: %s\n", ec.message().c_str());
        return false;
    }

    // Make executable
    std::filesystem::permissions(output_path,
                                std::filesystem::perms::owner_exec |
                                std::filesystem::perms::group_exec |
                                std::filesystem::perms::others_exec,
                                std::filesystem::perm_options::add, ec);
    if (ec) {
        std::fprintf(stderr, "[export] Failed to set permissions: %s\n", ec.message().c_str());
        return false;
    }

    // Copy Dawn dylib if present
    for (const auto& entry : std::filesystem::directory_iterator(build_path)) {
        auto fname = entry.path().filename().string();
        if (fname.find("wgpu") != std::string::npos &&
            (str_ends_with(fname, ".dylib") || str_ends_with(fname, ".so") || str_ends_with(fname, ".dll"))) {
            auto dst = output_path.parent_path() / fname;
            std::filesystem::copy(entry.path(), dst,
                                  std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                std::fprintf(stderr, "[export] Failed to copy %s: %s\n", fname.c_str(), ec.message().c_str());
                return false;
            }
            std::fprintf(stderr, "[export] Copied %s\n", fname.c_str());
        }
    }

    return true;
}

} // namespace vivid
