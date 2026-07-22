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
    // Own the enum choice-label strings. pb->choice_labels aliases the (still-alive) op's member
    // vector, which is destroyed when the temporary op that built this descriptor is — so copy the
    // strings into cache-owned storage now and repoint choice_labels at them (mirrors the owned
    // display_name/keywords). Without this, reading choices after the build crashes (dangling).
    out.m_choice_storage.assign(out.params.size(), {});
    out.m_choice_ptrs.assign(out.params.size(), {});
    for (size_t i = 0; i < out.params.size(); ++i) {
        VividParamDescriptor& pd = out.params[i];
        if (pd.choice_count == 0 || !pd.choice_labels) { pd.choice_labels = nullptr; continue; }
        for (uint32_t k = 0; k < pd.choice_count; ++k)
            out.m_choice_storage[i].push_back(pd.choice_labels[k] ? pd.choice_labels[k] : "");
        for (const auto& s : out.m_choice_storage[i]) out.m_choice_ptrs[i].push_back(s.c_str());
        pd.choice_labels = out.m_choice_ptrs[i].data();
    }
    // Own the FILE/TEXT default strings (str_value on the derived Param), so default_string
    // isn't null (descriptor validation) and outlives the temporary op that built this.
    out.m_file_default_storage.assign(out.params.size(), {});
    for (size_t i = 0; i < out.params.size(); ++i) {
        VividParamDescriptor& pd = out.params[i];
        if (pd.type == VIVID_PARAM_FILE) {
            out.m_file_default_storage[i] = static_cast<const Param<FilePath>*>(param_ptrs[i])->str_value;
            pd.default_string = out.m_file_default_storage[i].c_str();
        } else if (pd.type == VIVID_PARAM_TEXT) {
            out.m_file_default_storage[i] = static_cast<const Param<TextValue>*>(param_ptrs[i])->str_value;
            pd.default_string = out.m_file_default_storage[i].c_str();
        } else {
            pd.default_string = nullptr;
        }
    }
    out.ports = ports;

    VividOperatorDescriptor& d = out.desc;
    d = VividOperatorDescriptor{};
    d.name        = out.name.c_str();
    d.param_count = static_cast<uint32_t>(out.params.size());
    d.params      = out.params.empty() ? nullptr : out.params.data();
    d.port_count  = static_cast<uint32_t>(out.ports.size());
    d.ports       = out.ports.empty() ? nullptr : out.ports.data();
    // Capability flags: a built-in's process_* interfaces ARE the truth (dynamic_cast). A loaded
    // dylib adapter implements all three interfaces, so it hands back the dylib's real descriptor
    // (host_capability_descriptor) and we copy the flags from there.
    if (const VividOperatorDescriptor* cap = op.host_capability_descriptor()) {
        d.has_process_gpu   = cap->has_process_gpu;
        d.has_process_audio = cap->has_process_audio;
        d.has_process_frame = cap->has_process_frame;
    } else {
        d.has_process_gpu   = (dynamic_cast<GpuProcessable*>(&op) != nullptr) ? 1 : 0;
        d.has_process_audio = (dynamic_cast<AudioProcessable*>(&op) != nullptr) ? 1 : 0;
        d.has_process_frame = (dynamic_cast<FrameProcessable*>(&op) != nullptr) ? 1 : 0;
    }
    d.multiplicity_behavior = VIVID_MULTIPLICITY_SCALAR_ONLY;  // P1: scalar only (no lanes)
    // v14: audio-runtime role. A loaded dylib returns its vivid_audio_role() export here (via the
    // LoadedOperator override); a built-in returns DEFAULT (it is classified by the audio_op_mark_*
    // name tables instead). Sourced from a virtual, never read out of the dylib's descriptor struct.
    d.audio_role = op.declared_audio_role();
}

void sync_params(OpInstance& inst, const float* values, int count) {
    if (!values) return;
    const int n = std::min(count, static_cast<int>(inst.param_ptrs.size()));
    for (int i = 0; i < n; ++i) inst.param_ptrs[i]->value = values[i];
}

void OpRegistry::register_type(const std::string& name, Factory f) {
    if (find(name)) return;  // first registration wins
    entries_.push_back({ name, std::move(f), {} });
}

void OpRegistry::register_type(const std::string& name, Factory f, OpMeta meta) {
    if (find(name)) return;  // first registration wins
    entries_.push_back({ name, std::move(f), std::move(meta) });
}

void OpRegistry::unregister_type(const std::string& name) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
        if (it->name == name) { entries_.erase(it); break; }
    invalidate_descriptor(name);   // drop the cached descriptor (name-keyed, so no aliasing)
    reload_errors_.erase(name);
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
    // Overlay operator-level metadata (display_name/keywords/summary) for discovery,
    // with stable owned storage the descriptor's const char* fields point into.
    if (e->meta.has) {
        cache->m_display_name = e->meta.display_name;
        cache->m_summary      = e->meta.summary;
        cache->m_keywords     = e->meta.keywords;
        cache->m_keyword_ptrs.clear();
        for (const auto& k : cache->m_keywords) cache->m_keyword_ptrs.push_back(k.c_str());
        cache->desc.display_name  = cache->m_display_name.empty() ? nullptr : cache->m_display_name.c_str();
        cache->desc.summary       = cache->m_summary.empty()      ? nullptr : cache->m_summary.c_str();
        cache->desc.keywords      = cache->m_keyword_ptrs.empty() ? nullptr : cache->m_keyword_ptrs.data();
        cache->desc.keyword_count = static_cast<uint32_t>(cache->m_keyword_ptrs.size());
    }
    OpDescriptor* raw = cache.get();
    desc_cache_.push_back(std::move(cache));
    return raw;
}

const VividOperatorDescriptor* OpRegistry::descriptor_for(const std::string& name) const {
    OpDescriptor* d = ensure_descriptor(name);
    return d ? &d->desc : nullptr;
}

void OpRegistry::invalidate_descriptor(const std::string& name) {
    for (auto it = desc_cache_.begin(); it != desc_cache_.end(); ++it)
        if ((*it)->name == name) { desc_cache_.erase(it); return; }
}

void OpRegistry::set_reload_error(const std::string& name, const std::string& error) {
    if (error.empty()) reload_errors_.erase(name);
    else               reload_errors_[name] = error;
}
std::string OpRegistry::reload_error(const std::string& name) const {
    auto it = reload_errors_.find(name);
    return it == reload_errors_.end() ? std::string() : it->second;
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
