#include "gpu/op_runtime.h"

namespace vivid {

void build_descriptor(OperatorBase& op, const std::string& type_name,
                      const std::vector<ParamBase*>& param_ptrs,
                      const std::vector<VividPortDescriptor>& ports,
                      OpDescriptor& out) {
    out.name = type_name;
    out.params.clear();
    out.params.reserve(param_ptrs.size());
    for (const ParamBase* pb : param_ptrs) {
        VividParamDescriptor pd{};
        pd.name          = pb->name;                  // static string literal (op-owned)
        pd.type          = pb->type;
        pd.default_value = pb->default_value;
        pd.min_value     = pb->min_value;
        pd.max_value     = pb->max_value;
        pd.choice_labels = pb->choice_labels;
        pd.choice_count  = pb->choice_count;
        pd.group         = pb->group;
        pd.display_hint  = pb->display_hint;
        pd.semantic_tag    = pb->semantic_tag;
        pd.semantic_shape  = pb->semantic_shape;
        pd.semantic_unit   = pb->semantic_unit;
        pd.semantic_intent = pb->semantic_intent;
        pd.description     = pb->description;
        out.params.push_back(pd);
    }
    out.ports = ports;

    VividOperatorDescriptor& d = out.desc;
    d = VividOperatorDescriptor{};
    d.name        = out.name.c_str();
    d.param_count = static_cast<uint32_t>(out.params.size());
    d.params      = out.params.empty() ? nullptr : out.params.data();
    d.port_count  = static_cast<uint32_t>(out.ports.size());
    d.ports       = out.ports.empty() ? nullptr : out.ports.data();
    d.has_process_gpu   = (dynamic_cast<GpuProcessable*>(&op) != nullptr) ? 1 : 0;
    d.has_process_audio = 0;
    d.has_process_frame = 0;
    d.multiplicity_behavior = VIVID_MULTIPLICITY_SCALAR_ONLY;  // P1: scalar only (no lanes)
}

void sync_params(OpInstance& inst, const float* values, int count) {
    if (!values) return;
    const int n = std::min(count, static_cast<int>(inst.param_ptrs.size()));
    for (int i = 0; i < n; ++i) inst.param_ptrs[i]->value = values[i];
}

void OpRegistry::register_type(const std::string& name, Factory f) {
    if (find(name)) return;  // first registration wins
    entries_.push_back({ name, std::move(f) });
}

const OpRegistry::Entry* OpRegistry::find(const std::string& name) const {
    for (const auto& e : entries_) if (e.name == name) return &e;
    return nullptr;
}

bool OpRegistry::has(const std::string& name) const { return find(name) != nullptr; }

std::vector<std::string> OpRegistry::type_names() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const auto& e : entries_) out.push_back(e.name);
    return out;
}

OpDescriptor* OpRegistry::ensure_descriptor(const std::string& name) const {
    for (auto& d : desc_cache_) if (d->name == name) return d.get();
    const Entry* e = find(name);
    if (!e) return nullptr;
    std::unique_ptr<OperatorBase> tmp = e->factory();
    if (!tmp) return nullptr;
    std::vector<ParamBase*> params;
    std::vector<VividPortDescriptor> ports;
    tmp->collect_params(params);
    tmp->collect_ports(ports);
    auto cache = std::make_unique<OpDescriptor>();
    build_descriptor(*tmp, name, params, ports, *cache);
    OpDescriptor* raw = cache.get();
    desc_cache_.push_back(std::move(cache));
    return raw;
}

const VividOperatorDescriptor* OpRegistry::descriptor_for(const std::string& name) const {
    OpDescriptor* d = ensure_descriptor(name);
    return d ? &d->desc : nullptr;
}

std::optional<OpInstance> OpRegistry::create(const std::string& name,
                                             std::vector<DescriptorValidationIssue>& issues) const {
    const Entry* e = find(name);
    if (!e) return std::nullopt;
    std::unique_ptr<OperatorBase> op = e->factory();
    if (!op) return std::nullopt;

    OpInstance inst;
    inst.type_name = name;
    op->collect_params(inst.param_ptrs);
    op->collect_ports(inst.ports);
    for (const auto& p : inst.ports) {
        if (p.direction == VIVID_PORT_OUTPUT) ++inst.output_port_count;
        else                                  ++inst.input_port_count;
    }
    inst.op = std::move(op);

    if (const VividOperatorDescriptor* d = descriptor_for(name))
        issues = validate_descriptor(d);
    return inst;
}

}  // namespace vivid
