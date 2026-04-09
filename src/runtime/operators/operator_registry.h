#pragma once

#include "runtime/operators/operator_loader.h"
#include "runtime/graph/graph.h"  // OperatorPreset
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <mutex>

namespace vivid {

struct WgslOperatorConfig;
struct OperatorBase;
class Graph;

// Probed operator metadata — enough for UI catalog without a full dlopen
struct DeferredEntry {
    std::string dylib_path;
    VividOperatorDescriptor desc{};                // owned copy (pointers into vectors below)
    std::vector<VividParamDescriptor> params;       // owned param descriptors
    std::vector<VividPortDescriptor> ports;         // owned port descriptors
    std::vector<std::string> param_names;           // stable strings for param name pointers
    std::vector<std::string> port_names;            // stable strings for port name pointers
    std::vector<std::string> port_type_names;       // stable storage for VividPortDescriptor::type_name
    std::vector<std::string> port_stable_type_ids;  // stable storage for VividPortDescriptor::stable_type_id
    std::vector<std::string> default_strings;       // stable strings for default_string pointers
    std::vector<std::string> semantic_tags;         // stable strings for semantic_tag pointers
    std::vector<std::string> port_semantic_tags;    // stable strings for port semantic_tag pointers
    std::vector<std::string> port_semantic_shapes;  // stable strings for port semantic_shape pointers
    std::vector<std::string> port_semantic_intents; // stable strings for port semantic_intent pointers
    std::vector<std::string> port_descriptions;     // stable strings for port description pointers
    std::vector<std::string> semantic_shapes;       // stable strings for semantic_shape pointers
    std::vector<std::string> semantic_units;        // stable strings for semantic_unit pointers
    std::vector<std::string> semantic_intents;      // stable strings for semantic_intent pointers
    std::vector<std::string> descriptions;          // stable strings for description pointers
    std::vector<std::string> asset_kinds;           // stable strings for asset_kind pointers
    std::vector<std::vector<std::string>> choice_labels;     // owned choice label strings
    std::vector<std::vector<const char*>> choice_label_ptrs; // C pointer arrays into choice_labels
    std::vector<VividFileDropHandlerDescriptor> file_drop_handlers;
    std::vector<std::string> file_drop_labels;
    std::vector<std::string> file_drop_file_params;
    std::vector<std::string> file_drop_descriptions;
    std::vector<std::vector<std::string>> file_drop_extensions;
    std::vector<std::vector<const char*>> file_drop_extension_ptrs;
};

struct AbiMismatchDiagnostic {
    std::string plugin_path;
    std::string plugin_name;
    std::string package_name;  // empty when unknown
    uint32_t plugin_abi = 0;
    uint32_t runtime_abi = 0;
};

struct OperatorMapEntry {
    std::string type_name;
    std::string dylib_path;
    std::string package_name;   // empty for built-in
    std::string status;         // "loaded", "deferred", "abi_mismatch"
    uint32_t abi_version = 0;   // 0 if unknown
};

struct OperatorProvenance {
    std::string package_name;
    std::string package_path;
    bool package_built = false;
    bool abi_mismatch = false;
    bool load_failed = false;
    std::string failure_detail;
};

struct LoaderFailureDiagnostic {
    std::string plugin_path;
    std::string plugin_name;
    std::string package_name;  // empty when unknown
    std::string code;
    std::string message;
};

struct FileDropRegistration {
    std::string type_name;
    std::string label;
    std::string file_param;
    std::string description;
    std::string package_name;
    std::vector<std::string> extensions;
    int32_t priority = 0;
};

class OperatorRegistry {
public:
    ~OperatorRegistry();

    // Optional callback invoked after each plugin probe during scan_deferred.
    // Can be used to update a loading screen between dlopen calls.
    using ProgressCallback = std::function<void()>;
    void set_progress_callback(ProgressCallback cb) { progress_cb_ = std::move(cb); }

    bool scan(const char* directory);
    bool scan_deferred(const char* directory);       // probe-only scan (no full load)
    bool scan_shader_operators(const std::string& directory,
                               bool mark_user = false,
                               const std::string& package_name = "");
    bool load_for_graph(const Graph& graph);         // load only operators the graph uses
    void register_builtin(const std::string& type_name,
                          VividDescriptorFn, VividCreateFn, VividDestroyFn, VividProcessFrameFn);
    void register_alias(const std::string& alias_name, const std::string& canonical_type_name);
    // Find operator by type name. May lazy-load from deferred plugins.
    // Use find_loaded() for a lookup that never triggers loading.
    OperatorLoader* find(const std::string& type_name);
    OperatorLoader* find_loaded(const std::string& type_name);
    const VividOperatorDescriptor* probe_descriptor(const std::string& type_name) const;

    // Shader-backed operator management
    void register_shader_operator(std::shared_ptr<WgslOperatorConfig> config,
                                  bool mark_user = false,
                                  const std::string& package_name = "");
    void unregister_shader_operator(const std::string& name);
    void clear_shader_operators_in_dir(const std::string& directory);
    bool is_shader_operator(const std::string& name) const;
    bool is_user_shader_operator(const std::string& name) const;
    const WgslOperatorConfig* shader_operator_config(const std::string& name) const;
    const std::string* shader_operator_source(const std::string& name) const;

    // User-defined C++ operator management
    void register_user_operator(const std::string& name, const std::string& source_path);
    bool is_user_operator(const std::string& name) const;
    const std::string* user_operator_source(const std::string& name) const;

    // Load a brand-new operator from a dylib (for cloned operators)
    bool register_loaded_operator(const std::string& dylib_path);

    // Introspection
    std::vector<std::string> type_names() const;

    // Target ↔ type mapping
    const std::string* type_name_for_target(const std::string& target) const;
    std::string type_to_target(const std::string& type_name) const;

    // Hot-reload support
    bool reload_operator(const std::string& type_name, const std::string& new_dylib_path);

    // Factory presets (per-operator-type, read-only)
    bool scan_factory_presets(const std::string& directory);
    const std::vector<OperatorPreset>* factory_presets(const std::string& type_name) const;
    std::vector<std::string> factory_preset_names(const std::string& type_name) const;

    // Package provenance tracking
    void register_package(const std::string& package_name, const std::string& build_dir);
    void unregister_package_operator(const std::string& type_name);
    void clear_retired_package_loaders();
    void clear_deferred_probe_handles_for_dir(const std::string& directory);
    void clear_diagnostics_for_dir(const std::string& directory);
    const std::string* package_for_type(const std::string& type_name) const;
    bool is_package_operator(const std::string& type_name) const;
    std::vector<FileDropRegistration> file_drop_handlers() const;

    // ABI mismatch diagnostics captured during probing/loading.
    std::vector<AbiMismatchDiagnostic> abi_mismatch_diagnostics() const;
    std::vector<AbiMismatchDiagnostic> abi_mismatch_diagnostics_for_dir(const std::string& directory) const;
    bool has_abi_mismatch_diagnostics() const;
    std::vector<LoaderFailureDiagnostic> loader_failure_diagnostics() const;
    std::vector<LoaderFailureDiagnostic> loader_failure_diagnostics_for_dir(const std::string& directory) const;
    bool has_loader_failure_diagnostics() const;

    // Developer diagnostic: full map of every known operator with provenance.
    std::vector<OperatorMapEntry> operator_map() const;

    // Expected-operator provenance: tracks operators declared in package manifests,
    // even if the package isn't built. Enables "why is this operator missing?" diagnostics.
    void register_expected_operator(const std::string& type_name, OperatorProvenance provenance);
    const OperatorProvenance* operator_provenance(const std::string& type_name) const;

private:
    void record_loader_failure(const std::string& plugin_path,
                               const std::string& plugin_name,
                               const OperatorLoader::LastError& error);

    // Helper: extract target name from dylib path and register loader
    void register_target_mapping(const std::string& dylib_path, const std::string& type_name);

    std::unordered_map<std::string, std::shared_ptr<WgslOperatorConfig>> shader_operator_configs_;
    std::unordered_map<std::string, std::string> shader_operator_sources_;
    std::unordered_map<std::string, std::unique_ptr<OperatorLoader>> loaders_;
    std::unordered_map<std::string, DeferredEntry> deferred_;  // probed but not yet loaded
    std::unordered_map<std::string, std::string> target_to_type_;  // cmake target → descriptor name
    std::unordered_map<std::string, std::string> aliases_;         // alias -> canonical type
    // Keep probe handles alive to avoid dlclose-time destructor hangs in some plugins.
    // These are process-lifetime handles; the OS reclaims them on exit.
    struct DeferredProbeHandle {
        std::string plugin_path;
        void* handle = nullptr;
    };
    std::vector<DeferredProbeHandle> deferred_probe_handles_;
    std::unordered_set<std::string> user_shader_operator_types_;
    std::unordered_map<std::string, std::string> user_operator_sources_;
    std::unordered_map<std::string, std::string> type_to_package_;  // type_name → package_name
    std::vector<std::unique_ptr<OperatorLoader>> retired_package_loaders_;
    std::unordered_map<std::string, std::vector<OperatorPreset>> factory_presets_;  // type_name → presets
    std::unordered_map<std::string, AbiMismatchDiagnostic> abi_mismatch_by_path_;   // plugin path -> mismatch info
    std::unordered_map<std::string, LoaderFailureDiagnostic> loader_failure_by_path_; // plugin path -> load failure
    std::unordered_map<std::string, OperatorProvenance> expected_operators_;  // type_name → manifest provenance
    std::unordered_set<std::string> in_flight_loads_;  // type_name -> lazy load currently materializing
    ProgressCallback progress_cb_;
    mutable std::recursive_mutex mutex_;
};

} // namespace vivid
