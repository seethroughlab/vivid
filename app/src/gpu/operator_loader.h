#pragma once

#include "operator_api/types.h"
#include <string>

// Loads one operator from a dynamic library (.dylib/.so): dlopen + ABI check +
// symbol resolution + descriptor validation, then dispatches create/destroy and
// the process_* entry points through the resolved function pointers.
//
// This is the P2.1 right-sized lift of vivid-classic's OperatorLoader — the
// dlopen/ABI/dispatch core only. Stripped (vs classic): the data-driven/WGSL
// operator path, hot-reload compatibility classification (P2.4), custom port-type
// pre-registration, and the inspector/editor/midi/thumbnail accessors. wgpu-free
// (process_* are stored as opaque fn-ptrs; contexts cross only as pointers), so
// this compiles + tests headless.
namespace vivid {

// Result of comparing a live operator's descriptor against a hot-reload candidate:
//   Compatible        — identical layout; an in-place dylib swap is safe.
//   RecompileRequired — layout identical but multiplicity/strategy changed (for the
//                       here this is handled like Compatible — we recreate instances).
//   Incompatible      — param/port/gpu layout changed; reject the swap (restart instead).
enum class HotReloadCompat { Compatible, RecompileRequired, Incompatible };

// Classify a candidate descriptor change. Null old/new → Compatible (first load).
// Free function so it is unit-testable without a dylib.
HotReloadCompat classify_hot_reload(const VividOperatorDescriptor* old_desc,
                                    const VividOperatorDescriptor* new_desc);

class OperatorLoader {
public:
    struct LastError {
        std::string code;
        std::string message;
    };

    OperatorLoader() = default;
    ~OperatorLoader();

    // Move-only (owns a dlopen handle).
    OperatorLoader(const OperatorLoader&) = delete;
    OperatorLoader& operator=(const OperatorLoader&) = delete;
    OperatorLoader(OperatorLoader&& other) noexcept;
    OperatorLoader& operator=(OperatorLoader&& other) noexcept;

    // dlopen + validate. On failure returns false and sets last_error(); a prior
    // successful load is left untouched (atomic: opens the new handle first).
    bool load(const char* path);
    void unload();

    const VividOperatorDescriptor* descriptor() const { return desc_fn_ ? desc_fn_() : nullptr; }
    void* create_instance() const { return create_fn_ ? create_fn_() : nullptr; }
    void  destroy_instance(void* instance) const { if (destroy_fn_ && instance) destroy_fn_(instance); }

    void process_frame(void* instance, VividFrameContext* ctx) const { if (process_frame_fn_) process_frame_fn_(instance, ctx); }
    void process_audio(void* instance, VividAudioContext* ctx) const { if (process_audio_fn_) process_audio_fn_(instance, ctx); }
    void process_gpu(void* instance, struct VividGpuContext* ctx) const { if (process_gpu_fn_) process_gpu_fn_(instance, ctx); }

    const std::string& registration_mode() const { return registration_mode_; }
    bool is_loaded() const { return handle_ != nullptr; }
    const LastError& last_error() const { return last_error_; }

    // True if the most recent successful load() swapped in a descriptor whose
    // multiplicity/strategy changed (HotReloadCompat::RecompileRequired).
    bool reload_required_recompile() const { return reload_required_recompile_; }

private:
    void set_last_error(std::string code, std::string message);
    void clear_last_error();
    void move_from(OperatorLoader&& other) noexcept;

    void*                handle_            = nullptr;
    VividDescriptorFn    desc_fn_           = nullptr;
    VividCreateFn        create_fn_         = nullptr;
    VividDestroyFn       destroy_fn_        = nullptr;
    VividProcessFrameFn  process_frame_fn_  = nullptr;
    VividProcessAudioFn  process_audio_fn_  = nullptr;
    VividProcessGpuFn    process_gpu_fn_    = nullptr;
    std::string          registration_mode_ = "unknown";
    bool                 reload_required_recompile_ = false;
    LastError            last_error_{};
};

}  // namespace vivid
