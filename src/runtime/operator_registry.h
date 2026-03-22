#pragma once

#include "runtime/operator_loader.h"
#include "runtime/graph.h"  // OperatorPreset
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace vivid {

struct DataDrivenFilterConfig;
struct OperatorBase;
class Graph;

// Custom deleter that calls through OperatorLoader's paired destroy function.
// operator() is defined in operator_registry.cpp (OperatorBase is incomplete here).
struct BindableDeleter {
    OperatorLoader* loader = nullptr;
    void operator()(vivid::OperatorBase* p) const;
};
using BindableOpHandle = std::unique_ptr<vivid::OperatorBase, BindableDeleter>;

enum class RoleBindingValidation {
    kOk,
    kRoleNotFound,
    kNotBindable,
    kTypeNotAllowed,
    kDomainMismatch,
    kOutputNotFound,
};

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
    std::vector<std::string> semantic_shapes;       // stable strings for semantic_shape pointers
    std::vector<std::string> semantic_units;        // stable strings for semantic_unit pointers
    std::vector<std::string> semantic_intents;      // stable strings for semantic_intent pointers
    std::vector<std::vector<std::string>> choice_labels;     // owned choice label strings
    std::vector<std::vector<const char*>> choice_label_ptrs; // C pointer arrays into choice_labels
    std::vector<VividFileDropHandlerDescriptor> file_drop_handlers;
    std::vector<std::string> file_drop_labels;
    std::vector<std::string> file_drop_file_params;
    std::vector<std::string> file_drop_descriptions;
    std::vector<std::vector<std::string>> file_drop_extensions;
    std::vector<std::vector<const char*>> file_drop_extension_ptrs;
    bool bindable = false;
    // Role binding descriptor storage
    std::vector<VividRoleBindingDescriptor> role_bindings;
    std::vector<std::string> role_ids;
    std::vector<std::string> role_labels;
    std::vector<std::vector<std::string>> role_allowed_types;
    std::vector<std::vector<const char*>> role_allowed_type_ptrs;
    std::vector<std::vector<std::string>> role_pref_tags;
    std::vector<std::vector<const char*>> role_pref_tag_ptrs;
    std::vector<std::string> role_pref_output_names;
    std::vector<std::string> role_default_types;
};

struct AbiMismatchDiagnostic {
    std::string plugin_path;
    std::string plugin_name;
    std::string package_name;  // empty when unknown
    uint32_t plugin_abi = 0;
    uint32_t runtime_abi = 0;
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

    bool scan(const char* directory);
    bool scan_deferred(const char* directory);       // probe-only scan (no full load)
    bool scan_wgsl_presets(const std::string& directory);  // self-describing .wgsl filters
    bool load_for_graph(const Graph& graph);         // load only operators the graph uses
    void register_builtin(const std::string& type_name,
                          VividDescriptorFn, VividCreateFn, VividDestroyFn, VividProcessFn);
    void register_alias(const std::string& alias_name, const std::string& canonical_type_name);
    // Find operator by type name. May lazy-load from deferred plugins.
    // Use find_loaded() for a lookup that never triggers loading.
    OperatorLoader* find(const std::string& type_name);
    OperatorLoader* find_loaded(const std::string& type_name);
    const VividOperatorDescriptor* probe_descriptor(const std::string& type_name) const;

    // Create a standalone bindable control operator instance by type name.
    // Returns nullptr if the type doesn't exist or isn't bindable.
    // The returned handle calls through the loader's paired destroy function.
    BindableOpHandle create_bindable(const std::string& type_name);

    // Check if a type is bindable without loading it.
    bool is_bindable(const std::string& type_name) const;

    // User-defined filter management
    void register_user_filter(const std::string& name,
                              std::shared_ptr<DataDrivenFilterConfig> config);
    void unregister_user_filter(const std::string& name);
    bool is_user_filter(const std::string& name) const;

    // User-defined C++ operator management
    void register_user_operator(const std::string& name, const std::string& source_path);
    bool is_user_operator(const std::string& name) const;
    const std::string* user_operator_source(const std::string& name) const;

    // Load a brand-new operator from a dylib (for cloned operators)
    bool register_loaded_operator(const std::string& dylib_path);

    // WGSL preset accessors
    const std::shared_ptr<DataDrivenFilterConfig>* wgsl_config(const std::string& name) const;
    std::vector<std::string> wgsl_preset_names() const;
    bool is_wgsl_preset(const std::string& name) const;

    // Return sorted list of bindable operator type names.
    // If role is non-null, filters by the role's accepted_domain and allowed_operator_types.
    // Does not trigger lazy-loading.
    std::vector<std::string> bindable_candidates(
        const VividRoleBindingDescriptor* role = nullptr) const;

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

private:
    void record_loader_failure(const std::string& plugin_path,
                               const std::string& plugin_name,
                               const OperatorLoader::LastError& error);

    // Helper: extract target name from dylib path and register loader
    void register_target_mapping(const std::string& dylib_path, const std::string& type_name);

    std::unordered_map<std::string, std::shared_ptr<DataDrivenFilterConfig>> wgsl_configs_;
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
    std::unordered_set<std::string> user_filter_types_;
    std::unordered_map<std::string, std::string> user_operator_sources_;
    std::unordered_map<std::string, std::string> type_to_package_;  // type_name → package_name
    std::vector<std::unique_ptr<OperatorLoader>> retired_package_loaders_;
    std::unordered_map<std::string, std::vector<OperatorPreset>> factory_presets_;  // type_name → presets
    std::unordered_map<std::string, AbiMismatchDiagnostic> abi_mismatch_by_path_;   // plugin path -> mismatch info
    std::unordered_map<std::string, LoaderFailureDiagnostic> loader_failure_by_path_; // plugin path -> load failure
};

// Validate whether a role binding assignment is valid for a given role.
// output_name is the target output port name to bind (validated against the target descriptor).
RoleBindingValidation validate_role_binding(
    const VividOperatorDescriptor* host_desc,
    const std::string& role_id,
    const std::string& type_name,
    const std::string& output_name,
    const OperatorRegistry& registry);

// Return the names of output ports on target_desc that are compatible with role_desc.
// If role_desc has preferred_output_semantic_tags, only outputs whose semantic_tag
// matches one of the tags are returned.  Otherwise all output ports are returned.
std::vector<std::string> compatible_outputs(
    const VividOperatorDescriptor* target_desc,
    const VividRoleBindingDescriptor* role_desc);

} // namespace vivid
