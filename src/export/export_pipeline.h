#pragma once

#include "operator_api/port_type_registry.h"
#include "runtime/packages/project_lockfile.h"

#include <string>
#include <vector>
#include <unordered_map>

namespace vivid {

class OperatorRegistry;
class Graph;
class PackageManager;

struct ExportOptions {
    std::string graph_path;
    std::string output_name;       // output binary name (no extension)
    std::string output_path;       // final binary destination (default: output_name in cwd)
    std::string output_dir;        // directory for export build tree (default: <output_name>_export)
    bool headless = false;
    bool control_server = false;
    std::vector<std::string> extra_operators;  // additional operator types to include

    // Phase 7: lockfile strict mode. When `strict` is true, ExportPipeline::run
    // loads `lockfile_path` (or the sibling `vivid.lock` if empty) and calls
    // verify_lockfile against the live environment. Any `Mismatch` finding
    // aborts the export with a populated last-status on the pipeline.
    bool strict = false;
    std::string lockfile_path;
};

class ExportPipeline {
public:
    // source_dir: vivid source tree root (for headers, deps, operator sources)
    // build_dir:  vivid build directory (for operator_manifest.json, Dawn)
    ExportPipeline(const std::string& source_dir, const std::string& build_dir);

    // `pm` is required when opts.strict is true; otherwise may be nullptr.
    bool run(const ExportOptions& opts, OperatorRegistry& registry,
             PackageManager* pm = nullptr);

    // Populated when run() returns false due to a strict-mode verify failure.
    // The CLI uses these to emit structured error output with the right exit code.
    bool strict_verify_failed() const { return strict_verify_failed_; }
    const LockfileStatus& last_strict_verify_status() const {
        return last_strict_verify_status_;
    }
    // One of: "" (not triggered), "mismatch", "no_lockfile", "no_pm", "io_error".
    const std::string& last_strict_verify_error_kind() const {
        return last_strict_verify_error_kind_;
    }

private:
    bool load_manifest();
    bool resolve_operators(const Graph& graph, const ExportOptions& opts,
                           OperatorRegistry& registry);
    bool generate_static_registry();
    bool generate_embedded_graph(const std::string& graph_path);
    bool generate_embedded_shader_operators(OperatorRegistry& registry);
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
        bool has_custom_types = false;
    };
    std::vector<ResolvedOperator> resolved_ops_;
    std::vector<VividPortTypeInfo> required_custom_types_;
    std::vector<std::string> shader_operator_types_;
    bool needs_webgpu_ = false;
    bool needs_rtmidi_ = false;
    bool headless_ = false;
    bool control_server_ = false;

    // Phase 7: strict-mode verify state (populated by run() when opts.strict).
    bool strict_verify_failed_ = false;
    LockfileStatus last_strict_verify_status_;
    std::string last_strict_verify_error_kind_;
};

} // namespace vivid
