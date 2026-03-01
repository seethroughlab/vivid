#pragma once

#include "runtime/operator_loader.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace vivid {

struct DataDrivenFilterConfig;
class Graph;

// Probed operator metadata — enough for UI catalog without a full dlopen
struct DeferredEntry {
    std::string dylib_path;
    VividOperatorDescriptor desc{};                // owned copy (pointers into vectors below)
    std::vector<VividParamDescriptor> params;       // owned param descriptors
    std::vector<VividPortDescriptor> ports;         // owned port descriptors
    std::vector<std::string> param_names;           // stable strings for param name pointers
    std::vector<std::string> port_names;            // stable strings for port name pointers
    std::vector<std::string> default_strings;       // stable strings for default_string pointers
    std::vector<std::vector<std::string>> choice_labels;     // owned choice label strings
    std::vector<std::vector<const char*>> choice_label_ptrs; // C pointer arrays into choice_labels
};

class OperatorRegistry {
public:
    bool scan(const char* directory);
    bool scan_deferred(const char* directory);       // probe-only scan (no full load)
    bool scan_wgsl_presets(const std::string& directory);  // self-describing .wgsl filters
    bool load_for_graph(const Graph& graph);         // load only operators the graph uses
    void register_builtin(const std::string& type_name,
                          VividDescriptorFn, VividCreateFn, VividDestroyFn, VividProcessFn);
    // Find operator by type name. May lazy-load from deferred plugins.
    // Use find_loaded() for a lookup that never triggers loading.
    OperatorLoader* find(const std::string& type_name);
    OperatorLoader* find_loaded(const std::string& type_name);
    const VividOperatorDescriptor* probe_descriptor(const std::string& type_name) const;

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

    // Introspection
    std::vector<std::string> type_names() const;

    // Hot-reload support
    const std::string* type_name_for_target(const std::string& target) const;
    bool reload_operator(const std::string& type_name, const std::string& new_dylib_path);

private:
    // Helper: extract target name from dylib path and register loader
    void register_target_mapping(const std::string& dylib_path, const std::string& type_name);

    std::unordered_map<std::string, std::shared_ptr<DataDrivenFilterConfig>> wgsl_configs_;
    std::unordered_map<std::string, std::unique_ptr<OperatorLoader>> loaders_;
    std::unordered_map<std::string, DeferredEntry> deferred_;  // probed but not yet loaded
    std::unordered_map<std::string, std::string> target_to_type_;  // cmake target → descriptor name
    std::unordered_set<std::string> user_filter_types_;
    std::unordered_map<std::string, std::string> user_operator_sources_;
};

} // namespace vivid
