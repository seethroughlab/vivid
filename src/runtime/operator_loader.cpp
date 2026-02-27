#include "runtime/operator_loader.h"
#include "operator_api/data_driven_filter.h"
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
    , draw_thumb_fn_(other.draw_thumb_fn_)
    , main_update_fn_(other.main_update_fn_)
    , dd_config_(std::move(other.dd_config_))
    , dd_name_(std::move(other.dd_name_))
    , dd_param_names_(std::move(other.dd_param_names_))
    , dd_params_(std::move(other.dd_params_))
    , dd_ports_(std::move(other.dd_ports_))
    , dd_desc_(other.dd_desc_)
{
    other.handle_        = nullptr;
    other.desc_fn_       = nullptr;
    other.create_fn_     = nullptr;
    other.destroy_fn_    = nullptr;
    other.process_fn_    = nullptr;
    other.draw_thumb_fn_ = nullptr;
    other.main_update_fn_ = nullptr;
    other.dd_desc_ = {};
    // Fixup descriptor pointers to our own storage
    if (dd_config_) {
        dd_desc_.name = dd_name_.c_str();
        dd_desc_.params = dd_params_.data();
        dd_desc_.ports = dd_ports_.data();
    }
}

OperatorLoader& OperatorLoader::operator=(OperatorLoader&& other) noexcept {
    if (this != &other) {
        unload();
        handle_        = other.handle_;
        desc_fn_       = other.desc_fn_;
        create_fn_     = other.create_fn_;
        destroy_fn_    = other.destroy_fn_;
        process_fn_    = other.process_fn_;
        draw_thumb_fn_ = other.draw_thumb_fn_;
        main_update_fn_ = other.main_update_fn_;
        dd_config_     = std::move(other.dd_config_);
        dd_name_       = std::move(other.dd_name_);
        dd_param_names_ = std::move(other.dd_param_names_);
        dd_params_     = std::move(other.dd_params_);
        dd_ports_      = std::move(other.dd_ports_);
        dd_desc_       = other.dd_desc_;
        other.handle_        = nullptr;
        other.desc_fn_       = nullptr;
        other.create_fn_     = nullptr;
        other.destroy_fn_    = nullptr;
        other.process_fn_    = nullptr;
        other.draw_thumb_fn_ = nullptr;
        other.main_update_fn_ = nullptr;
        other.dd_desc_ = {};
        // Fixup descriptor pointers to our own storage
        if (dd_config_) {
            dd_desc_.name = dd_name_.c_str();
            dd_desc_.params = dd_params_.data();
            dd_desc_.ports = dd_ports_.data();
        }
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

    // Optional: custom thumbnail drawing
    draw_thumb_fn_ = reinterpret_cast<VividDrawThumbnailFn>(dlsym(handle_, "vivid_draw_thumbnail"));

    // Optional: main-thread update hook (for audio operators with file I/O)
    main_update_fn_ = reinterpret_cast<VividMainThreadUpdateFn>(dlsym(handle_, "vivid_main_thread_update"));

    return true;
}

void OperatorLoader::init_builtin(VividDescriptorFn desc, VividCreateFn create,
                                   VividDestroyFn destroy, VividProcessFn process) {
    unload();
    desc_fn_    = desc;
    create_fn_  = create;
    destroy_fn_ = destroy;
    process_fn_ = process;
}

void OperatorLoader::init_data_driven(std::shared_ptr<DataDrivenFilterConfig> config) {
    unload();
    dd_config_ = std::move(config);

    // Build owned descriptor with stable string storage
    dd_name_ = dd_config_->name;
    dd_param_names_.clear();
    dd_params_.clear();
    for (const auto& pd : dd_config_->params) {
        dd_param_names_.push_back(pd.name);
    }
    dd_params_.resize(dd_config_->params.size());
    for (size_t i = 0; i < dd_config_->params.size(); ++i) {
        auto& dp = dd_params_[i];
        const auto& sp = dd_config_->params[i];
        dp.name = dd_param_names_[i].c_str();
        dp.type = sp.type;
        dp.default_value = sp.default_value;
        dp.min_value = sp.min_value;
        dp.max_value = sp.max_value;
        dp.choice_labels = nullptr;
        dp.choice_count = 0;
    }

    // Standard GPU filter ports: 1 input texture + 1 output texture
    dd_ports_ = {
        {"input",   VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT},
        {"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT},
    };

    dd_desc_.name = dd_name_.c_str();
    dd_desc_.domain = VIVID_DOMAIN_GPU;
    dd_desc_.param_count = static_cast<uint32_t>(dd_params_.size());
    dd_desc_.params = dd_params_.data();
    dd_desc_.port_count = static_cast<uint32_t>(dd_ports_.size());
    dd_desc_.ports = dd_ports_.data();
    dd_desc_.time_dependent = dd_config_->time_dependent ? 1 : 0;
}

void OperatorLoader::unload() {
    if (handle_) {
        dlclose(handle_);
        handle_        = nullptr;
        desc_fn_       = nullptr;
        create_fn_     = nullptr;
        destroy_fn_    = nullptr;
        process_fn_    = nullptr;
        draw_thumb_fn_ = nullptr;
        main_update_fn_ = nullptr;
    }
    if (dd_config_) {
        dd_config_.reset();
        dd_name_.clear();
        dd_param_names_.clear();
        dd_params_.clear();
        dd_ports_.clear();
        dd_desc_ = {};
    }
}

const VividOperatorDescriptor* OperatorLoader::descriptor() const {
    if (dd_config_) return &dd_desc_;
    return desc_fn_ ? desc_fn_() : nullptr;
}

void* OperatorLoader::create_instance() const {
    if (dd_config_) return new DataDrivenFilter(dd_config_);
    return create_fn_ ? create_fn_() : nullptr;
}

void OperatorLoader::destroy_instance(void* instance) const {
    if (dd_config_) {
        delete static_cast<DataDrivenFilter*>(instance);
        return;
    }
    if (destroy_fn_ && instance) {
        destroy_fn_(instance);
    }
}

void OperatorLoader::process(void* instance, const VividProcessContext* ctx) const {
    if (dd_config_) {
        static_cast<WgslFilterBase*>(instance)->process(ctx);
        return;
    }
    if (process_fn_ && instance) {
        process_fn_(instance, ctx);
    }
}

void OperatorLoader::draw_thumbnail(void* instance, const VividThumbnailContext* ctx) const {
    if (draw_thumb_fn_ && instance) {
        draw_thumb_fn_(instance, ctx);
    }
}

void OperatorLoader::main_thread_update(void* instance, double time,
                                         const char** file_param_values,
                                         uint32_t file_param_count) const {
    if (main_update_fn_ && instance) {
        main_update_fn_(instance, time, file_param_values, file_param_count);
    }
}

} // namespace vivid
