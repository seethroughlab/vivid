#include "export/export_pipeline.h"
#include "runtime/operator_registry.h"
#include "runtime/tool_discovery.h"
#include "runtime/graph.h"
#include "operator_api/data_driven_filter.h"
#include "yyjson.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <algorithm>

namespace vivid {

static std::string quote(const std::string& s) {
    return "'" + s + "'";
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
    if (!generate_embedded_wgsl_presets(registry)) return false;
    if (!generate_cmakelists()) return false;
    if (!copy_standalone_main()) return false;

    // 5. Build
    if (!build()) return false;

    // 6. Copy output
    if (!copy_output(opts.output_name)) return false;

    std::fprintf(stderr, "[export] Success! Binary: %s\n", opts.output_name.c_str());
    return true;
}

bool ExportPipeline::load_manifest() {
    std::string path = build_dir_ + "/operator_manifest.json";

    yyjson_read_err err;
    yyjson_doc* doc = yyjson_read_file(path.c_str(), 0, nullptr, &err);
    if (!doc) {
        std::fprintf(stderr, "[export] Failed to read manifest: %s: %s\n", path.c_str(), err.msg);
        return false;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);
    yyjson_val* key;
    while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
        yyjson_val* val = yyjson_obj_iter_get_val(key);
        std::string target = yyjson_get_str(key);

        ManifestEntry entry;

        yyjson_val* sources = yyjson_obj_get(val, "sources");
        if (sources && yyjson_is_arr(sources)) {
            size_t idx, max;
            yyjson_val* s;
            yyjson_arr_foreach(sources, idx, max, s) {
                if (yyjson_is_str(s))
                    entry.sources.push_back(yyjson_get_str(s));
            }
        }

        yyjson_val* libs = yyjson_obj_get(val, "extra_libs");
        if (libs && yyjson_is_arr(libs)) {
            size_t idx, max;
            yyjson_val* l;
            yyjson_arr_foreach(libs, idx, max, l) {
                if (yyjson_is_str(l))
                    entry.extra_libs.push_back(yyjson_get_str(l));
            }
        }

        yyjson_val* fws = yyjson_obj_get(val, "frameworks");
        if (fws && yyjson_is_arr(fws)) {
            size_t idx, max;
            yyjson_val* f;
            yyjson_arr_foreach(fws, idx, max, f) {
                if (yyjson_is_str(f))
                    entry.frameworks.push_back(yyjson_get_str(f));
            }
        }

        yyjson_val* arcs = yyjson_obj_get(val, "objc_arc");
        if (arcs && yyjson_is_arr(arcs)) {
            size_t idx, max;
            yyjson_val* a;
            yyjson_arr_foreach(arcs, idx, max, a) {
                if (yyjson_is_str(a))
                    entry.objc_arc.push_back(yyjson_get_str(a));
            }
        }

        yyjson_val* incs = yyjson_obj_get(val, "include_dirs");
        if (incs && yyjson_is_arr(incs)) {
            size_t idx, max;
            yyjson_val* d;
            yyjson_arr_foreach(incs, idx, max, d) {
                if (yyjson_is_str(d))
                    entry.include_dirs.push_back(yyjson_get_str(d));
            }
        }

        manifest_[target] = std::move(entry);
    }

    yyjson_doc_free(doc);
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
    wgsl_preset_names_.clear();
    needs_webgpu_ = false;
    needs_rtmidi_ = false;

    for (const auto& type_name : needed_types) {
        // Skip builtins (audio_out, video_out) — they're compiled into the standalone main
        if (type_name == "audio_out" || type_name == "video_out")
            continue;

        // Check if it's a WGSL preset
        if (registry.is_wgsl_preset(type_name)) {
            wgsl_preset_names_.push_back(type_name);
            needs_webgpu_ = true;
            continue;
        }

        // Check if it's a user filter defined in the graph JSON
        bool is_graph_filter = false;
        for (const auto& fd : graph.filters()) {
            if (fd.name == type_name) {
                is_graph_filter = true;
                break;
            }
        }
        if (is_graph_filter) {
            needs_webgpu_ = true;
            continue;  // handled by embedded graph + WGSLFilter
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

        resolved_ops_.push_back({target, type_name, mit->second});
        std::fprintf(stderr, "[export] Resolved: %s -> %s\n", type_name.c_str(), target.c_str());
    }

    // Sort for deterministic output
    std::sort(resolved_ops_.begin(), resolved_ops_.end(),
              [](const auto& a, const auto& b) { return a.target < b.target; });
    std::sort(wgsl_preset_names_.begin(), wgsl_preset_names_.end());

    std::fprintf(stderr, "[export] %zu operators, %zu WGSL presets\n",
                 resolved_ops_.size(), wgsl_preset_names_.size());
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

    // Forward declare renamed symbols for each operator
    for (const auto& op : resolved_ops_) {
        out << "extern \"C\" const VividOperatorDescriptor* vivid_descriptor_" << op.target << "();\n";
        out << "extern \"C\" void* vivid_create_" << op.target << "();\n";
        out << "extern \"C\" void vivid_destroy_" << op.target << "(void*);\n";
        out << "extern \"C\" void vivid_process_" << op.target << "(void*, const VividProcessContext*);\n";
    }

    out << "\nvoid register_static_operators(vivid::OperatorRegistry& registry) {\n";
    for (const auto& op : resolved_ops_) {
        out << "    registry.register_builtin(\"" << op.type_name << "\",\n"
            << "        vivid_descriptor_" << op.target << ",\n"
            << "        vivid_create_" << op.target << ",\n"
            << "        vivid_destroy_" << op.target << ",\n"
            << "        vivid_process_" << op.target << ");\n";
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

bool ExportPipeline::generate_embedded_wgsl_presets(OperatorRegistry& registry) {
    if (wgsl_preset_names_.empty()) return true;

    std::string path = export_dir_ + "/embedded_wgsl_presets.cpp";
    std::ofstream out(path);
    if (!out) {
        std::fprintf(stderr, "[export] Failed to create %s\n", path.c_str());
        return false;
    }

    out << "// Generated by vivid export — do not edit\n";
    out << "#include \"runtime/operator_registry.h\"\n";
    out << "#include \"runtime/wgsl_header_parser.h\"\n";
    out << "#include \"operator_api/data_driven_filter.h\"\n";
    out << "#include <string>\n";
    out << "#include <cstdio>\n";
    out << "#include <fstream>\n";
    out << "#include <filesystem>\n\n";

    // Embed each WGSL shader as a string literal
    for (const auto& name : wgsl_preset_names_) {
        const auto* config = registry.wgsl_config(name);
        if (!config || !*config) continue;

        std::ifstream ifs((*config)->shader_path);
        if (!ifs) {
            std::fprintf(stderr, "[export] WARNING: cannot read WGSL shader for %s: %s\n",
                         name.c_str(), (*config)->shader_path.c_str());
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

    // Registration function: writes temp files and registers via registry
    out << "void register_embedded_wgsl_presets(vivid::OperatorRegistry& registry) {\n";
    out << "    namespace fs = std::filesystem;\n";
    out << "    auto tmp_dir = fs::temp_directory_path() / \"vivid_standalone_wgsl\";\n";
    out << "    fs::create_directories(tmp_dir);\n\n";

    for (const auto& name : wgsl_preset_names_) {
        const auto* config = registry.wgsl_config(name);
        if (!config || !*config) continue;

        out << "    {\n";
        out << "        std::string path = (tmp_dir / \"" << name << ".wgsl\").string();\n";
        out << "        { std::ofstream ofs(path); ofs << kWGSL_" << name << "; }\n";
        out << "        std::string error;\n";
        out << "        auto header = vivid::parse_wgsl_header(kWGSL_" << name << ", error);\n";
        out << "        if (header) {\n";
        out << "            auto cfg = std::make_shared<vivid::DataDrivenFilterConfig>();\n";
        out << "            cfg->name = header->name;\n";
        out << "            cfg->shader_path = path;\n";
        out << "            cfg->time_dependent = header->time_dependent;\n";
        out << "            cfg->inputs_specified = header->inputs_specified;\n";
        out << "            for (const auto& inp : header->inputs)\n";
        out << "                cfg->inputs.push_back({inp.name});\n";
        out << "            for (const auto& hp : header->params) {\n";
        out << "                vivid::DataDrivenFilterConfig::ParamDef pd;\n";
        out << "                pd.name = hp.name; pd.type = hp.type;\n";
        out << "                pd.default_value = hp.default_value;\n";
        out << "                pd.min_value = hp.min_value; pd.max_value = hp.max_value;\n";
        out << "                pd.label = hp.label; pd.choices = hp.choices;\n";
        out << "                pd.display_hint = hp.display_hint; pd.group = hp.group;\n";
        out << "                pd.layout_columns = hp.layout_columns;\n";
        out << "                pd.layout_column_index = hp.layout_column_index;\n";
        out << "                cfg->params.push_back(std::move(pd));\n";
        out << "            }\n";
        out << "            registry.register_user_filter(cfg->name, cfg);\n";
        out << "            std::fprintf(stderr, \"[standalone] Registered WGSL preset: %s\\n\",\n";
        out << "                         cfg->name.c_str());\n";
        out << "        } else {\n";
        out << "            std::fprintf(stderr, \"[standalone] Failed to parse WGSL preset "
            << name << ": %s\\n\", error.c_str());\n";
        out << "        }\n";
        out << "    }\n";
    }

    out << "}\n";

    std::fprintf(stderr, "[export] Generated embedded_wgsl_presets.cpp (%zu presets)\n",
                 wgsl_preset_names_.size());
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
                  << "    vivid_process=vivid_process_" << op.target << "\n"
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
    if (!wgsl_preset_names_.empty()) {
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
    std::string configure_cmd = quote(cmake_exe) + " -S " + quote(export_dir_) + " -B " + quote(build_path) +
                                " -DCMAKE_BUILD_TYPE=Release 2>&1";
    std::fprintf(stderr, "[export] Configuring: %s\n", configure_cmd.c_str());
    int rc = std::system(configure_cmd.c_str());
    if (rc != 0) {
        std::fprintf(stderr, "[export] CMake configure failed (exit %d)\n", rc);
        return false;
    }

    // Build
    std::string build_cmd = quote(cmake_exe) + " --build " + quote(build_path) + " --config Release 2>&1";
    std::fprintf(stderr, "[export] Building: %s\n", build_cmd.c_str());
    rc = std::system(build_cmd.c_str());
    if (rc != 0) {
        std::fprintf(stderr, "[export] Build failed (exit %d)\n", rc);
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

    // Copy binary
    std::error_code ec;
    std::filesystem::copy(src_binary, output_name,
                          std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fprintf(stderr, "[export] Failed to copy binary: %s\n", ec.message().c_str());
        return false;
    }

    // Make executable
    std::filesystem::permissions(output_name,
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
            auto dst = std::filesystem::path(output_name).parent_path() / fname;
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
