#include "runtime/operator_loader.h"
#include <dlfcn.h>
#include <cstdio>
#include <utility>

namespace vivid {

OperatorLoader::~OperatorLoader() {
    unload();
}

OperatorLoader::OperatorLoader(OperatorLoader&& other) noexcept
    : handle_(other.handle_)
    , desc_fn_(other.desc_fn_)
    , create_fn_(other.create_fn_)
    , destroy_fn_(other.destroy_fn_)
    , process_fn_(other.process_fn_)
{
    other.handle_     = nullptr;
    other.desc_fn_    = nullptr;
    other.create_fn_  = nullptr;
    other.destroy_fn_ = nullptr;
    other.process_fn_ = nullptr;
}

OperatorLoader& OperatorLoader::operator=(OperatorLoader&& other) noexcept {
    if (this != &other) {
        unload();
        handle_     = other.handle_;
        desc_fn_    = other.desc_fn_;
        create_fn_  = other.create_fn_;
        destroy_fn_ = other.destroy_fn_;
        process_fn_ = other.process_fn_;
        other.handle_     = nullptr;
        other.desc_fn_    = nullptr;
        other.create_fn_  = nullptr;
        other.destroy_fn_ = nullptr;
        other.process_fn_ = nullptr;
    }
    return *this;
}

bool OperatorLoader::load(const char* path) {
    unload();

    handle_ = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        std::fprintf(stderr, "[vivid] dlopen failed: %s\n", dlerror());
        return false;
    }

    // Resolve all four entry points
    desc_fn_    = reinterpret_cast<VividDescriptorFn>(dlsym(handle_, "vivid_descriptor"));
    create_fn_  = reinterpret_cast<VividCreateFn>(dlsym(handle_, "vivid_create"));
    destroy_fn_ = reinterpret_cast<VividDestroyFn>(dlsym(handle_, "vivid_destroy"));
    process_fn_ = reinterpret_cast<VividProcessFn>(dlsym(handle_, "vivid_process"));

    if (!desc_fn_)    std::fprintf(stderr, "[vivid] Missing symbol: vivid_descriptor\n");
    if (!create_fn_)  std::fprintf(stderr, "[vivid] Missing symbol: vivid_create\n");
    if (!destroy_fn_) std::fprintf(stderr, "[vivid] Missing symbol: vivid_destroy\n");
    if (!process_fn_) std::fprintf(stderr, "[vivid] Missing symbol: vivid_process\n");

    if (!desc_fn_ || !create_fn_ || !destroy_fn_ || !process_fn_) {
        unload();
        return false;
    }

    return true;
}

void OperatorLoader::unload() {
    if (handle_) {
        dlclose(handle_);
        handle_     = nullptr;
        desc_fn_    = nullptr;
        create_fn_  = nullptr;
        destroy_fn_ = nullptr;
        process_fn_ = nullptr;
    }
}

const VividOperatorDescriptor* OperatorLoader::descriptor() const {
    return desc_fn_ ? desc_fn_() : nullptr;
}

void* OperatorLoader::create_instance() const {
    return create_fn_ ? create_fn_() : nullptr;
}

void OperatorLoader::destroy_instance(void* instance) const {
    if (destroy_fn_ && instance) {
        destroy_fn_(instance);
    }
}

void OperatorLoader::process(void* instance, const VividProcessContext* ctx) const {
    if (process_fn_ && instance) {
        process_fn_(instance, ctx);
    }
}

} // namespace vivid
