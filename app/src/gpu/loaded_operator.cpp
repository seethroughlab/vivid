#include "gpu/loaded_operator.h"

namespace vivid {

LoadedOperator::LoadedOperator(const OperatorLoader* loader) : loader_(loader) {
    if (!loader_) return;
    instance_ = loader_->create_instance();

    const VividOperatorDescriptor* d = loader_->descriptor();
    if (!d) return;

    // Mirror each param descriptor into a synthetic ParamBase. Only the fields
    // build_descriptor() reads are copied; the const char* fields alias the dylib's
    // static storage (it outlives this adapter). value starts at the default.
    synth_params_.reserve(d->param_count);
    for (uint32_t i = 0; i < d->param_count; ++i) {
        const VividParamDescriptor& pd = d->params[i];
        ParamBase pb{};
        pb.name            = pd.name;
        pb.type            = pd.type;
        pb.default_value   = pd.default_value;
        pb.value           = pd.default_value;
        pb.min_value       = pd.min_value;
        pb.max_value       = pd.max_value;
        pb.choice_labels   = pd.choice_labels;
        pb.choice_count    = pd.choice_count;
        pb.group           = pd.group;
        pb.display_hint    = pd.display_hint;
        pb.semantic_tag    = pd.semantic_tag;
        pb.semantic_shape  = pd.semantic_shape;
        pb.semantic_unit   = pd.semantic_unit;
        pb.semantic_intent = pd.semantic_intent;
        pb.description     = pd.description;
        synth_params_.push_back(pb);
    }
    synth_param_ptrs_.reserve(synth_params_.size());
    for (auto& p : synth_params_) synth_param_ptrs_.push_back(&p);

    if (d->ports && d->port_count > 0)
        ports_.assign(d->ports, d->ports + d->port_count);
}

LoadedOperator::~LoadedOperator() {
    if (loader_ && instance_) loader_->destroy_instance(instance_);
}

void LoadedOperator::collect_params(std::vector<ParamBase*>& out) { out = synth_param_ptrs_; }
void LoadedOperator::collect_ports(std::vector<VividPortDescriptor>& out) { out = ports_; }

// Each stage forwards to the dylib's resolved fn-ptr (a no-op if the dylib didn't export
// that stage). The runtimes only invoke the stage the descriptor's has_process_* declares,
// so a gpu-only op never has its (no-op) process_audio called, and vice-versa.
void LoadedOperator::process_gpu(const VividGpuContext* ctx) {
    if (loader_ && instance_)
        loader_->process_gpu(instance_, const_cast<VividGpuContext*>(ctx));
}
void LoadedOperator::process_audio(const VividAudioContext* ctx) {
    if (loader_ && instance_)
        loader_->process_audio(instance_, const_cast<VividAudioContext*>(ctx));
}
void LoadedOperator::process_frame(const VividFrameContext* ctx) {
    if (loader_ && instance_)
        loader_->process_frame(instance_, const_cast<VividFrameContext*>(ctx));
}

void LoadedOperator::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (loader_ && instance_) loader_->draw_thumbnail(instance_, ctx);   // no-op if the dylib exports none
}
VividAudioRole LoadedOperator::declared_audio_role() const {
    return loader_ ? loader_->audio_role() : VIVID_AUDIO_ROLE_DEFAULT;
}

const VividOperatorDescriptor* LoadedOperator::host_capability_descriptor() const {
    return loader_ ? loader_->descriptor() : nullptr;
}

}  // namespace vivid
