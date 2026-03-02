#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace vivid {

class OperatorRegistry;
class Graph;

struct ExportOptions {
    std::string graph_path;
    std::string output_name;       // output binary name (no extension)
    std::string output_dir;        // directory for export build tree (default: <output_name>_export)
    bool headless = false;
    bool control_server = false;
    std::vector<std::string> extra_operators;  // additional operator types to include
};

class ExportPipeline {
public:
    // source_dir: vivid source tree root (for headers, deps, operator sources)
    // build_dir:  vivid build directory (for operator_manifest.json, Dawn)
    ExportPipeline(const std::string& source_dir, const std::string& build_dir);

    bool run(const ExportOptions& opts, OperatorRegistry& registry);

private:
    bool load_manifest();
    bool resolve_operators(const Graph& graph, const ExportOptions& opts,
                           OperatorRegistry& registry);
    bool generate_static_registry();
    bool generate_embedded_graph(const std::string& graph_path);
    bool generate_embedded_wgsl_presets(OperatorRegistry& registry);
    bool generate_cmakelists();
    bool copy_standalone_main();
    bool build();
    bool copy_output(const std::string& output_name);

    std::string source_dir_;
    std::string build_dir_;
    std::string export_dir_;

    struct ManifestEntry {
        std::vector<std::string> sources;
        std::vector<std::string> extra_libs;
        std::vector<std::string> frameworks;
        std::vector<std::string> objc_arc;
        std::vector<std::string> include_dirs;
    };
    std::unordered_map<std::string, ManifestEntry> manifest_;

    // Resolved for current export
    struct ResolvedOperator {
        std::string target;       // cmake target name
        std::string type_name;    // operator type name
        ManifestEntry manifest;
    };
    std::vector<ResolvedOperator> resolved_ops_;
    std::vector<std::string> wgsl_preset_names_;
    bool needs_webgpu_ = false;
    bool needs_rtmidi_ = false;
    bool headless_ = false;
    bool control_server_ = false;
};

} // namespace vivid
