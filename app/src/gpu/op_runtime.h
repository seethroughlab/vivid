#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "operator_api/operator.h"
#include "operator_api/operator_descriptor_validation.h"

// In-process operator runtime: a name->factory registry that instantiates lifted
// operators, builds + validates their descriptors, and syncs resolved param
// values into them. No dlopen/packages (that's P2) — operators are compiled in
// and registered directly. wgpu-free, so this + its test stay headless.
namespace vivid {

// One installed operator instance: the op + its collected param/port metadata.
// param_ptrs is in collect_params() order — the canonical param index that the
// descriptor, the resolved param_values array, and sync_params all share.
struct OpInstance {
    std::unique_ptr<OperatorBase>     op;
    std::vector<ParamBase*>           param_ptrs;
    std::vector<VividPortDescriptor>  ports;
    std::string                       type_name;
    int input_port_count  = 0;
    int output_port_count = 0;
};

// Stable per-type descriptor storage (the VividOperatorDescriptor holds raw
// pointers into these vectors; all pointer fields copied from ParamBase point at
// static string literals owned by the operator class, so they outlive a temp).
struct OpDescriptor {
    std::string                        name;
    std::vector<VividParamDescriptor>  params;
    std::vector<VividPortDescriptor>   ports;
    VividOperatorDescriptor            desc{};
};

class OpRegistry {
public:
    using Factory = std::function<std::unique_ptr<OperatorBase>()>;

    void register_type(const std::string& name, Factory f);
    bool has(const std::string& name) const;
    std::vector<std::string> type_names() const;   // registration order

    // Instantiate `name`: create, collect params/ports, validate the descriptor.
    // Returns nullopt for an unknown type; `issues` carries any validation issues
    // (a valid op returns an instance with empty issues).
    std::optional<OpInstance> create(const std::string& name,
                                     std::vector<DescriptorValidationIssue>& issues) const;

    // Cached descriptor for a type (built once, stable address). nullptr if unknown.
    const VividOperatorDescriptor* descriptor_for(const std::string& name) const;

private:
    struct Entry { std::string name; Factory factory; };
    std::vector<Entry> entries_;
    const Entry* find(const std::string& name) const;
    // lazily-built per-type descriptors (stable via unique_ptr).
    mutable std::vector<std::unique_ptr<OpDescriptor>> desc_cache_;
    OpDescriptor* ensure_descriptor(const std::string& name) const;
};

// Build a descriptor into `out` from a live operator's collected params/ports.
void build_descriptor(OperatorBase& op, const std::string& type_name,
                      const std::vector<ParamBase*>& param_ptrs,
                      const std::vector<VividPortDescriptor>& ports,
                      OpDescriptor& out);

// Write resolved values into the instance's params (collect_params order).
// Copies min(count, param_ptrs.size()) values.
void sync_params(OpInstance& inst, const float* values, int count);

}  // namespace vivid
