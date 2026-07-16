#include "gpu/operator_loader.h"
#include "operator_api/operator_descriptor_validation.h"

#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace vivid {

namespace {
// The runtime ABI the loader requires of a dylib. Overridable via the
// VIVID_MOCK_RUNTIME_ABI env var so a test can force an ABI mismatch against a
// correctly-built fixture (mirrors classic's test hook).
uint32_t expected_abi() {
    if (const char* env = std::getenv("VIVID_MOCK_RUNTIME_ABI"))
        return static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    return VIVID_OPERATOR_ABI_VERSION;
}

bool streq(const char* a, const char* b) {
    return std::strcmp(a ? a : "", b ? b : "") == 0;
}

// Old params must be a prefix of new (params may be appended, not changed/removed),
// with matching name + type — so a node's resolved-value indices stay valid.
bool param_layout_compatible(const VividOperatorDescriptor* o, const VividOperatorDescriptor* n) {
    if (o->param_count > n->param_count) return false;
    for (uint32_t i = 0; i < o->param_count; ++i)
        if (!streq(o->params[i].name, n->params[i].name) || o->params[i].type != n->params[i].type)
            return false;
    return true;
}

// Ports must match exactly (count, name, type, direction, value_type, multiplicity)
// so existing wires remain valid.
bool port_layout_compatible(const VividOperatorDescriptor* o, const VividOperatorDescriptor* n) {
    if (o->port_count != n->port_count) return false;
    for (uint32_t i = 0; i < o->port_count; ++i) {
        const auto& a = o->ports[i]; const auto& b = n->ports[i];
        if (!streq(a.name, b.name) || a.type != b.type || a.direction != b.direction ||
            a.value_type != b.value_type || a.multiplicity != b.multiplicity)
            return false;
    }
    return true;
}
}  // namespace

HotReloadCompat classify_hot_reload(const VividOperatorDescriptor* old_desc,
                                    const VividOperatorDescriptor* new_desc) {
    if (!old_desc || !new_desc) return HotReloadCompat::Compatible;   // first load
    if (old_desc->has_process_gpu != new_desc->has_process_gpu) return HotReloadCompat::Incompatible;
    if (!param_layout_compatible(old_desc, new_desc)) return HotReloadCompat::Incompatible;
    if (!port_layout_compatible(old_desc, new_desc))  return HotReloadCompat::Incompatible;
    if (old_desc->multiplicity_behavior != new_desc->multiplicity_behavior)
        return HotReloadCompat::RecompileRequired;
    return HotReloadCompat::Compatible;
}

OperatorLoader::~OperatorLoader() { unload(); }

OperatorLoader::OperatorLoader(OperatorLoader&& other) noexcept { move_from(std::move(other)); }

OperatorLoader& OperatorLoader::operator=(OperatorLoader&& other) noexcept {
    if (this != &other) { unload(); move_from(std::move(other)); }
    return *this;
}

void OperatorLoader::move_from(OperatorLoader&& o) noexcept {
    handle_            = o.handle_;
    desc_fn_           = o.desc_fn_;
    create_fn_         = o.create_fn_;
    destroy_fn_        = o.destroy_fn_;
    process_frame_fn_  = o.process_frame_fn_;
    process_audio_fn_  = o.process_audio_fn_;
    process_gpu_fn_    = o.process_gpu_fn_;
    editor_meta_fn_    = o.editor_meta_fn_;
    draw_editor_fn_    = o.draw_editor_fn_;
    drop_fn_           = o.drop_fn_;
    registration_mode_ = std::move(o.registration_mode_);
    reload_required_recompile_ = o.reload_required_recompile_;
    last_error_        = std::move(o.last_error_);
    o.handle_ = nullptr;
    o.desc_fn_ = nullptr; o.create_fn_ = nullptr; o.destroy_fn_ = nullptr;
    o.process_frame_fn_ = nullptr; o.process_audio_fn_ = nullptr; o.process_gpu_fn_ = nullptr;
    o.editor_meta_fn_ = nullptr; o.draw_editor_fn_ = nullptr; o.drop_fn_ = nullptr;
}

void OperatorLoader::set_last_error(std::string code, std::string message) {
    last_error_ = { std::move(code), std::move(message) };
}
void OperatorLoader::clear_last_error() { last_error_ = {}; }

void OperatorLoader::unload() {
    if (handle_) { dlclose(handle_); handle_ = nullptr; }
    desc_fn_ = nullptr; create_fn_ = nullptr; destroy_fn_ = nullptr;
    process_frame_fn_ = nullptr; process_audio_fn_ = nullptr; process_gpu_fn_ = nullptr;
    editor_meta_fn_ = nullptr; draw_editor_fn_ = nullptr; drop_fn_ = nullptr;
    registration_mode_ = "unknown";
}

// All the resolved state of a candidate dylib — filled by open_and_check(), then either committed
// into the loader's members (load) or discarded (validate).
struct OperatorLoader::Resolved {
    void*                     handle          = nullptr;
    VividDescriptorFn         desc_fn         = nullptr;
    VividCreateFn             create_fn       = nullptr;
    VividDestroyFn            destroy_fn      = nullptr;
    VividProcessFrameFn       frame_fn        = nullptr;
    VividProcessAudioFn       audio_fn        = nullptr;
    VividProcessGpuFn         gpu_fn          = nullptr;
    VividEditorMetadataFn     editor_meta_fn  = nullptr;
    VividDrawEditorFn         draw_editor_fn  = nullptr;
    VividFileDropDescriptorFn drop_fn         = nullptr;
    std::string               mode;
    bool                      reload_required_recompile = false;
};

bool OperatorLoader::open_and_check(const char* path, Resolved& out) {
    // Open the candidate; a prior load stays live until the CALLER commits (load) or discards it.
    out.handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!out.handle) {
        const char* e = dlerror();
        set_last_error("dlopen_failed", e ? e : "unknown dlopen error");
        std::fprintf(stderr, "[vivid] dlopen failed: %s\n", last_error_.message.c_str());
        return false;
    }
    void* h = out.handle;

    auto abi_fn = reinterpret_cast<VividAbiVersionFn>(dlsym(h, "vivid_abi_version"));
    if (!abi_fn) {
        set_last_error("missing_abi_symbol", "missing symbol: vivid_abi_version");
        dlclose(h); out.handle = nullptr;
        return false;
    }
    const uint32_t abi = abi_fn();
    // Accept any ABI in [MIN_LOADABLE, current]. The ABI only grows by APPENDING fields, so an
    // operator built against an older one still finds every field it knows at the same offset and
    // simply ignores the rest. Demanding an exact match meant a purely additive bump (v11 -> v12,
    // note output) silently orphaned every operator dylib a user had already installed.
    // A NEWER-than-us dylib is still rejected: it may expect fields we don't provide.
    if (abi < VIVID_OPERATOR_ABI_MIN_LOADABLE || abi > expected_abi()) {
        set_last_error("abi_mismatch",
                       "plugin ABI " + std::to_string(abi) + " not loadable by runtime ABI " +
                       std::to_string(expected_abi()) + " (min " +
                       std::to_string(VIVID_OPERATOR_ABI_MIN_LOADABLE) + ")");
        std::fprintf(stderr, "[vivid] %s (%s)\n", last_error_.message.c_str(), path);
        dlclose(h); out.handle = nullptr;
        return false;
    }
    if (abi != expected_abi())
        std::fprintf(stderr, "[vivid] operator built at ABI %u, running on %u (compatible) — %s\n",
                     abi, expected_abi(), path);

    out.desc_fn    = reinterpret_cast<VividDescriptorFn>(dlsym(h, "vivid_descriptor"));
    out.create_fn  = reinterpret_cast<VividCreateFn>(dlsym(h, "vivid_create"));
    out.destroy_fn = reinterpret_cast<VividDestroyFn>(dlsym(h, "vivid_destroy"));
    out.frame_fn   = reinterpret_cast<VividProcessFrameFn>(dlsym(h, "vivid_process_frame"));
    out.audio_fn   = reinterpret_cast<VividProcessAudioFn>(dlsym(h, "vivid_process_audio"));
    out.gpu_fn     = reinterpret_cast<VividProcessGpuFn>(dlsym(h, "vivid_process_gpu"));
    if (!out.desc_fn || !out.create_fn || !out.destroy_fn || (!out.frame_fn && !out.audio_fn && !out.gpu_fn)) {
        set_last_error("missing_required_symbols",
                       "missing one or more required entry points "
                       "(vivid_descriptor/create/destroy + a process_*)");
        dlclose(h); out.handle = nullptr;
        return false;
    }

    const VividOperatorDescriptor* desc = out.desc_fn();
    if (!desc) {
        set_last_error("null_descriptor", "vivid_descriptor returned null");
        dlclose(h); out.handle = nullptr;
        return false;
    }
    if (!desc->name || !*desc->name) {
        set_last_error("invalid_descriptor_name", "descriptor has a missing/empty name");
        dlclose(h); out.handle = nullptr;
        return false;
    }

    auto mode_fn = reinterpret_cast<VividRegistrationModeFn>(dlsym(h, "vivid_registration_mode"));
    auto uniform_fn = reinterpret_cast<VividGeneratedUniformLayoutFn>(
        dlsym(h, "vivid_generated_uniform_layout"));
    // UI-4b: optional custom editor. Both symbols must be present to opt in; a partial
    // export (only one) is treated as no editor.
    auto editor_meta_fn = reinterpret_cast<VividEditorMetadataFn>(dlsym(h, "vivid_editor_metadata"));
    auto draw_editor_fn = reinterpret_cast<VividDrawEditorFn>(dlsym(h, "vivid_draw_editor"));
    // ADR-0021/P3: optional file-drop handlers.
    out.drop_fn = reinterpret_cast<VividFileDropDescriptorFn>(dlsym(h, "vivid_file_drop_descriptor"));
    out.mode = (mode_fn && mode_fn() && *mode_fn()) ? mode_fn() : "legacy";
    const VividGeneratedUniformLayout* uniform_layout = uniform_fn ? uniform_fn() : nullptr;

    auto issues = validate_descriptor(desc, out.mode.c_str(), uniform_layout);
    if (!issues.empty()) {
        set_last_error("invalid_descriptor", issues.front().code + ": " + issues.front().message);
        std::fprintf(stderr, "[vivid] invalid descriptor for %s: %s\n", path, last_error_.message.c_str());
        dlclose(h); out.handle = nullptr;
        return false;
    }

    // Hot-reload classification against the currently-loaded descriptor: reject a swap
    // whose param/port/gpu layout changed (it could drop wires / remap params).
    const HotReloadCompat compat = classify_hot_reload(descriptor(), desc);
    if (compat == HotReloadCompat::Incompatible) {
        set_last_error("hot_reload_incompatible",
                       "param/port/gpu layout changed; restart to load this change");
        std::fprintf(stderr, "[vivid] hot-reload rejected for %s: incompatible layout change\n", path);
        dlclose(h); out.handle = nullptr;
        return false;
    }
    out.reload_required_recompile = (compat == HotReloadCompat::RecompileRequired);
    out.editor_meta_fn = (editor_meta_fn && draw_editor_fn) ? editor_meta_fn : nullptr;   // both-or-neither
    out.draw_editor_fn = (editor_meta_fn && draw_editor_fn) ? draw_editor_fn : nullptr;
    return true;
}

bool OperatorLoader::validate(const char* path) {
    clear_last_error();
    Resolved r;
    const bool ok = open_and_check(path, r);
    if (r.handle) dlclose(r.handle);   // never commit — the current load is untouched
    return ok;
}

bool OperatorLoader::load(const char* path) {
    clear_last_error();
    Resolved r;
    if (!open_and_check(path, r)) return false;   // candidate closed by open_and_check on failure

    // Commit: release any prior dylib, install the new state.
    unload();
    handle_            = r.handle;
    desc_fn_           = r.desc_fn;
    create_fn_         = r.create_fn;
    destroy_fn_        = r.destroy_fn;
    process_frame_fn_  = r.frame_fn;
    process_audio_fn_  = r.audio_fn;
    process_gpu_fn_    = r.gpu_fn;
    editor_meta_fn_    = r.editor_meta_fn;
    draw_editor_fn_    = r.draw_editor_fn;
    drop_fn_           = r.drop_fn;
    registration_mode_ = r.mode;
    reload_required_recompile_ = r.reload_required_recompile;
    return true;
}

}  // namespace vivid
