#pragma once

#include "operator_api/operator.h"
#include "gpu/operator_loader.h"
#include <vector>

// Adapts a dlopen'd operator (an opaque void* instance + extern "C" fn-ptrs +
// a C descriptor) to the in-process OperatorBase + capability interfaces, so a
// loaded operator flows through OpRegistry::create() → build_descriptor() →
// the visual/audio runtimes identically to a built-in. It implements ALL THREE
// process interfaces and forwards to the dylib's resolved fn-ptr (a no-op if the
// dylib didn't export that stage), so audio/frame/gpu operator packages all run —
// not just gpu. Which stage(s) an op ACTUALLY has is the dylib descriptor's
// has_process_* flags (see declared_descriptor()); build_descriptor copies those
// rather than inferring capability from this adapter's C++ inheritance. The dylib's
// params are mirrored into synthetic ParamBase objects (build_descriptor reads only
// plain ParamBase fields, so the mirror is lossless); the dylib re-syncs its own
// params from ctx->param_values inside its process fn.
namespace vivid {

class LoadedOperator : public OperatorBase, public GpuProcessable,
                       public AudioProcessable, public FrameProcessable {
public:
    // `loader` is non-owning and must outlive this operator (App owns the loaders).
    explicit LoadedOperator(const OperatorLoader* loader);
    ~LoadedOperator() override;

    void collect_params(std::vector<ParamBase*>& out) override;
    void collect_ports(std::vector<VividPortDescriptor>& out) override;
    void process_gpu(const VividGpuContext* ctx) override;
    void process_audio(const VividAudioContext* ctx) override;
    void process_frame(const VividFrameContext* ctx) override;

    // v14: forward the dylib's optional thumbnail + declared audio role (dlsym'd), so a project
    // operator draws its own cell preview and classifies as a generator/note-fx/modulator.
    void draw_thumbnail(const VividThumbnailContext* ctx) override;
    VividAudioRole declared_audio_role() const override;
    bool host_syncs_file_params() const override { return false; }

    // The dylib's own descriptor — the authority on which process_* stages this op has
    // (this adapter inherits all three interfaces, so a dynamic_cast can't tell them apart).
    const VividOperatorDescriptor* host_capability_descriptor() const override;

    // UI-4b: operator-exported custom editor (dlsym'd vivid_editor_metadata + vivid_draw_editor).
    // The host dispatches through these so the opaque dylib instance never leaves this adapter.
    bool has_editor() const { return loader_ && loader_->has_editor(); }
    VividEditorMetadata editor_metadata() const { return loader_ ? loader_->editor_metadata() : VividEditorMetadata{}; }
    void draw_editor(VividEditorContext* ctx) const { if (loader_) loader_->draw_editor(instance_, ctx); }

private:
    const OperatorLoader*            loader_   = nullptr;
    void*                            instance_ = nullptr;
    std::vector<ParamBase>           synth_params_;       // mirrored from the dylib descriptor
    std::vector<ParamBase*>          synth_param_ptrs_;   // collect_params order
    std::vector<VividPortDescriptor> ports_;              // copied from the dylib descriptor
};

}  // namespace vivid
