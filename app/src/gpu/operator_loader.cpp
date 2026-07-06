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
    registration_mode_ = std::move(o.registration_mode_);
    reload_required_recompile_ = o.reload_required_recompile_;
    last_error_        = std::move(o.last_error_);
    o.handle_ = nullptr;
    o.desc_fn_ = nullptr; o.create_fn_ = nullptr; o.destroy_fn_ = nullptr;
    o.process_frame_fn_ = nullptr; o.process_audio_fn_ = nullptr; o.process_gpu_fn_ = nullptr;
}

void OperatorLoader::set_last_error(std::string code, std::string message) {
    last_error_ = { std::move(code), std::move(message) };
}
void OperatorLoader::clear_last_error() { last_error_ = {}; }

void OperatorLoader::unload() {
    if (handle_) { dlclose(handle_); handle_ = nullptr; }
    desc_fn_ = nullptr; create_fn_ = nullptr; destroy_fn_ = nullptr;
    process_frame_fn_ = nullptr; process_audio_fn_ = nullptr; process_gpu_fn_ = nullptr;
    registration_mode_ = "unknown";
}

bool OperatorLoader::load(const char* path) {
    clear_last_error();

    // Open the new handle first; a prior load stays live until we commit.
    void* new_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!new_handle) {
        const char* e = dlerror();
        set_last_error("dlopen_failed", e ? e : "unknown dlopen error");
        std::fprintf(stderr, "[vivid] dlopen failed: %s\n", last_error_.message.c_str());
        return false;
    }

    auto abi_fn = reinterpret_cast<VividAbiVersionFn>(dlsym(new_handle, "vivid_abi_version"));
    if (!abi_fn) {
        set_last_error("missing_abi_symbol", "missing symbol: vivid_abi_version");
        dlclose(new_handle);
        return false;
    }
    const uint32_t abi = abi_fn();
    if (abi != expected_abi()) {
        set_last_error("abi_mismatch",
                       "plugin ABI " + std::to_string(abi) + " != runtime ABI " +
                       std::to_string(expected_abi()));
        std::fprintf(stderr, "[vivid] %s (%s)\n", last_error_.message.c_str(), path);
        dlclose(new_handle);
        return false;
    }

    auto desc_fn    = reinterpret_cast<VividDescriptorFn>(dlsym(new_handle, "vivid_descriptor"));
    auto create_fn  = reinterpret_cast<VividCreateFn>(dlsym(new_handle, "vivid_create"));
    auto destroy_fn = reinterpret_cast<VividDestroyFn>(dlsym(new_handle, "vivid_destroy"));
    auto frame_fn   = reinterpret_cast<VividProcessFrameFn>(dlsym(new_handle, "vivid_process_frame"));
    auto audio_fn   = reinterpret_cast<VividProcessAudioFn>(dlsym(new_handle, "vivid_process_audio"));
    auto gpu_fn     = reinterpret_cast<VividProcessGpuFn>(dlsym(new_handle, "vivid_process_gpu"));
    if (!desc_fn || !create_fn || !destroy_fn || (!frame_fn && !audio_fn && !gpu_fn)) {
        set_last_error("missing_required_symbols",
                       "missing one or more required entry points "
                       "(vivid_descriptor/create/destroy + a process_*)");
        dlclose(new_handle);
        return false;
    }

    const VividOperatorDescriptor* desc = desc_fn();
    if (!desc) {
        set_last_error("null_descriptor", "vivid_descriptor returned null");
        dlclose(new_handle);
        return false;
    }
    if (!desc->name || !*desc->name) {
        set_last_error("invalid_descriptor_name", "descriptor has a missing/empty name");
        dlclose(new_handle);
        return false;
    }

    auto mode_fn = reinterpret_cast<VividRegistrationModeFn>(dlsym(new_handle, "vivid_registration_mode"));
    auto uniform_fn = reinterpret_cast<VividGeneratedUniformLayoutFn>(
        dlsym(new_handle, "vivid_generated_uniform_layout"));
    const std::string mode = (mode_fn && mode_fn() && *mode_fn()) ? mode_fn() : "legacy";
    const VividGeneratedUniformLayout* uniform_layout = uniform_fn ? uniform_fn() : nullptr;

    auto issues = validate_descriptor(desc, mode.c_str(), uniform_layout);
    if (!issues.empty()) {
        set_last_error("invalid_descriptor", issues.front().code + ": " + issues.front().message);
        std::fprintf(stderr, "[vivid] invalid descriptor for %s: %s\n", path, last_error_.message.c_str());
        dlclose(new_handle);
        return false;
    }

    // Hot-reload classification against the currently-loaded descriptor: reject a swap
    // whose param/port/gpu layout changed (it could drop wires / remap params).
    const HotReloadCompat compat = classify_hot_reload(descriptor(), desc);
    if (compat == HotReloadCompat::Incompatible) {
        set_last_error("hot_reload_incompatible",
                       "param/port/gpu layout changed; restart to load this change");
        std::fprintf(stderr, "[vivid] hot-reload rejected for %s: incompatible layout change\n", path);
        dlclose(new_handle);
        return false;
    }
    reload_required_recompile_ = (compat == HotReloadCompat::RecompileRequired);

    // Commit: release any prior dylib, install the new state.
    unload();
    handle_            = new_handle;
    desc_fn_           = desc_fn;
    create_fn_         = create_fn;
    destroy_fn_        = destroy_fn;
    process_frame_fn_  = frame_fn;
    process_audio_fn_  = audio_fn;
    process_gpu_fn_    = gpu_fn;
    registration_mode_ = mode;
    return true;
}

}  // namespace vivid
