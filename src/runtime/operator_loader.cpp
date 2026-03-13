#include "runtime/operator_loader.h"
#include "operator_api/data_driven_filter.h"
#include "operator_api/port_type_registry.h"
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
    , process_audio_fn_(other.process_audio_fn_)
    , process_gpu_fn_(other.process_gpu_fn_)
    , draw_thumb_fn_(other.draw_thumb_fn_)
    , main_update_fn_(other.main_update_fn_)
    , dd_config_(std::move(other.dd_config_))
    , dd_name_(std::move(other.dd_name_))
    , dd_param_names_(std::move(other.dd_param_names_))
    , dd_group_strings_(std::move(other.dd_group_strings_))
    , dd_choice_labels_(std::move(other.dd_choice_labels_))
    , dd_choice_ptrs_(std::move(other.dd_choice_ptrs_))
    , dd_params_(std::move(other.dd_params_))
    , dd_port_names_(std::move(other.dd_port_names_))
    , dd_ports_(std::move(other.dd_ports_))
    , dd_desc_(other.dd_desc_)
{
    other.handle_           = nullptr;
    other.desc_fn_          = nullptr;
    other.create_fn_        = nullptr;
    other.destroy_fn_       = nullptr;
    other.process_fn_       = nullptr;
    other.process_audio_fn_ = nullptr;
    other.process_gpu_fn_   = nullptr;
    other.draw_thumb_fn_    = nullptr;
    other.main_update_fn_   = nullptr;
    other.dd_desc_ = {};
    // Fixup descriptor pointers to our own storage
    if (dd_config_) {
        fixup_dd_pointers();
    }
}

OperatorLoader& OperatorLoader::operator=(OperatorLoader&& other) noexcept {
    if (this != &other) {
        unload();
        handle_           = other.handle_;
        desc_fn_          = other.desc_fn_;
        create_fn_        = other.create_fn_;
        destroy_fn_       = other.destroy_fn_;
        process_fn_       = other.process_fn_;
        process_audio_fn_ = other.process_audio_fn_;
        process_gpu_fn_   = other.process_gpu_fn_;
        draw_thumb_fn_    = other.draw_thumb_fn_;
        main_update_fn_   = other.main_update_fn_;
        dd_config_        = std::move(other.dd_config_);
        dd_name_          = std::move(other.dd_name_);
        dd_param_names_   = std::move(other.dd_param_names_);
        dd_group_strings_ = std::move(other.dd_group_strings_);
        dd_choice_labels_ = std::move(other.dd_choice_labels_);
        dd_choice_ptrs_   = std::move(other.dd_choice_ptrs_);
        dd_params_        = std::move(other.dd_params_);
        dd_port_names_    = std::move(other.dd_port_names_);
        dd_ports_         = std::move(other.dd_ports_);
        dd_desc_          = other.dd_desc_;
        other.handle_           = nullptr;
        other.desc_fn_          = nullptr;
        other.create_fn_        = nullptr;
        other.destroy_fn_       = nullptr;
        other.process_fn_       = nullptr;
        other.process_audio_fn_ = nullptr;
        other.process_gpu_fn_   = nullptr;
        other.draw_thumb_fn_    = nullptr;
        other.main_update_fn_   = nullptr;
        other.dd_desc_ = {};
        // Fixup descriptor pointers to our own storage
        if (dd_config_) {
            fixup_dd_pointers();
        }
    }
    return *this;
}

bool OperatorLoader::load(const char* path) {
    // Attempt to open the new dylib before touching current state (atomic swap).
    void* new_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!new_handle) {
        const char* dl_err = dlerror();
        std::fprintf(stderr, "[vivid] dlopen failed: %s\n", dl_err ? dl_err : "unknown error");
        return false;  // Old dylib still live
    }

    // Resolve required symbols from new handle
    auto abi_fn = reinterpret_cast<VividAbiVersionFn>(dlsym(new_handle, "vivid_abi_version"));
    if (!abi_fn) {
        std::fprintf(stderr, "[vivid] Missing symbol: vivid_abi_version (stale/incompatible plugin)\n");
        dlclose(new_handle);
        return false;
    }
    const uint32_t abi = abi_fn();
    if (abi != VIVID_OPERATOR_ABI_VERSION) {
        std::fprintf(stderr, "[vivid] Incompatible plugin ABI: got %u, expected %u\n",
                     abi, VIVID_OPERATOR_ABI_VERSION);
        dlclose(new_handle);
        return false;
    }

    auto new_desc_fn    = reinterpret_cast<VividDescriptorFn>(dlsym(new_handle, "vivid_descriptor"));
    auto new_create_fn  = reinterpret_cast<VividCreateFn>(dlsym(new_handle, "vivid_create"));
    auto new_destroy_fn = reinterpret_cast<VividDestroyFn>(dlsym(new_handle, "vivid_destroy"));

    // Per-domain process entry points
    auto new_process_fn       = reinterpret_cast<VividProcessFn>(dlsym(new_handle, "vivid_process"));
    auto new_process_audio_fn = reinterpret_cast<VividProcessAudioFn>(dlsym(new_handle, "vivid_process_audio"));
    auto new_process_gpu_fn   = reinterpret_cast<VividProcessGpuFn>(dlsym(new_handle, "vivid_process_gpu"));

    if (!new_desc_fn)    std::fprintf(stderr, "[vivid] Missing symbol: vivid_descriptor\n");
    if (!new_create_fn)  std::fprintf(stderr, "[vivid] Missing symbol: vivid_create\n");
    if (!new_destroy_fn) std::fprintf(stderr, "[vivid] Missing symbol: vivid_destroy\n");
    // At least one process entry point must exist
    if (!new_process_fn && !new_process_audio_fn && !new_process_gpu_fn)
        std::fprintf(stderr, "[vivid] Missing symbol: no process entry point found\n");

    if (!new_desc_fn || !new_create_fn || !new_destroy_fn ||
        (!new_process_fn && !new_process_audio_fn && !new_process_gpu_fn)) {
        dlclose(new_handle);
        return false;  // Old dylib still live
    }

    // All symbols resolved — commit the swap: release old dylib, install new state
    if (handle_) {
        if (dlclose(handle_) != 0) {
            const char* dl_err = dlerror();
            std::fprintf(stderr, "[vivid] dlclose failed: %s\n", dl_err ? dl_err : "unknown error");
        }
    }
    // Clear any data-driven state (we are now loading a native dylib)
    if (dd_config_) {
        dd_config_.reset();
        dd_name_.clear();
        dd_param_names_.clear();
        dd_group_strings_.clear();
        dd_choice_labels_.clear();
        dd_choice_ptrs_.clear();
        dd_params_.clear();
        dd_port_names_.clear();
        dd_ports_.clear();
        dd_desc_ = {};
    }

    handle_           = new_handle;
    desc_fn_          = new_desc_fn;
    create_fn_        = new_create_fn;
    destroy_fn_       = new_destroy_fn;
    process_fn_       = new_process_fn;
    process_audio_fn_ = new_process_audio_fn;
    process_gpu_fn_   = new_process_gpu_fn;

    // Optional entry points
    draw_thumb_fn_  = reinterpret_cast<VividDrawThumbnailFn>(dlsym(new_handle, "vivid_draw_thumbnail"));
    main_update_fn_ = reinterpret_cast<VividMainThreadUpdateFn>(dlsym(new_handle, "vivid_main_thread_update"));

    // Optional: register custom port types declared by this dylib (pull model).
    // The operator exports a static array of VividPortTypeInfo; the runtime
    // registers each entry. This avoids operators calling runtime symbols
    // across the dlopen boundary.
    if (auto* desc_fn = reinterpret_cast<VividDescribeCustomTypesFn>(
            dlsym(new_handle, "vivid_describe_custom_types"))) {
        uint32_t count = 0;
        const VividPortTypeInfo* infos = desc_fn(&count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!vivid_register_port_type(&infos[i])) {
                std::fprintf(stderr, "[vivid] Failed to register custom port type for plugin: %s\n", path);
                dlclose(new_handle);
                handle_ = nullptr;
                desc_fn_ = nullptr;
                create_fn_ = nullptr;
                destroy_fn_ = nullptr;
                process_fn_ = nullptr;
                process_audio_fn_ = nullptr;
                process_gpu_fn_ = nullptr;
                draw_thumb_fn_ = nullptr;
                main_update_fn_ = nullptr;
                return false;
            }
        }
    }

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
    dd_group_strings_.clear();
    dd_choice_labels_.clear();
    dd_choice_ptrs_.clear();
    dd_params_.clear();

    const size_t param_count = dd_config_->params.size();
    dd_param_names_.resize(param_count);
    dd_group_strings_.resize(param_count);
    dd_choice_labels_.resize(param_count);
    dd_choice_ptrs_.resize(param_count);
    dd_params_.resize(param_count);

    for (size_t i = 0; i < param_count; ++i) {
        const auto& sp = dd_config_->params[i];
        auto& dp = dd_params_[i];

        // Use label as display name if provided, otherwise use param name
        dd_param_names_[i] = sp.label.empty() ? sp.name : sp.label;
        dp.name = dd_param_names_[i].c_str();
        dp.type = sp.type;
        dp.default_value = sp.default_value;
        dp.min_value = sp.min_value;
        dp.max_value = sp.max_value;
        dp.default_string = nullptr;
        dp.display_hint = sp.display_hint;
        dp.layout_columns = sp.layout_columns;
        dp.layout_column_index = sp.layout_column_index;
        dp.semantic_tag = nullptr;
        dp.semantic_shape = nullptr;
        dp.semantic_unit = nullptr;
        dp.semantic_intent = nullptr;

        // Group
        dd_group_strings_[i] = sp.group;
        dp.group = sp.group.empty() ? nullptr : dd_group_strings_[i].c_str();

        // Choices
        if (!sp.choices.empty()) {
            dd_choice_labels_[i] = sp.choices;
            dd_choice_ptrs_[i].resize(sp.choices.size());
            for (size_t j = 0; j < sp.choices.size(); ++j)
                dd_choice_ptrs_[i][j] = dd_choice_labels_[i][j].c_str();
            dp.choice_labels = dd_choice_ptrs_[i].data();
            dp.choice_count = static_cast<uint32_t>(sp.choices.size());
        } else {
            dp.choice_labels = nullptr;
            dp.choice_count = 0;
        }
    }

    // Build ports from config inputs (or default 1-in/1-out)
    dd_port_names_.clear();
    dd_ports_.clear();
    if (dd_config_->inputs_specified) {
        dd_port_names_.reserve(dd_config_->inputs.size());
        for (const auto& inp : dd_config_->inputs) {
            dd_port_names_.push_back(inp.name);
            dd_ports_.push_back({dd_port_names_.back().c_str(),
                                  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        }
    } else {
        dd_port_names_.push_back("input");
        dd_ports_.push_back({dd_port_names_.back().c_str(),
                              VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
    }
    dd_port_names_.push_back("texture");
    dd_ports_.push_back({dd_port_names_.back().c_str(),
                          VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});

    dd_desc_.domain = VIVID_DOMAIN_GPU;
    dd_desc_.param_count = static_cast<uint32_t>(dd_params_.size());
    dd_desc_.port_count = static_cast<uint32_t>(dd_ports_.size());
    dd_desc_.time_dependent = dd_config_->time_dependent ? 1 : 0;
    dd_desc_.has_process_audio = 0;
    dd_desc_.has_process_gpu = 1;
    // Re-point all const char* after vectors are final (push_back may have reallocated)
    fixup_dd_pointers();
}

void OperatorLoader::fixup_dd_pointers() {
    dd_desc_.name = dd_name_.c_str();
    for (size_t i = 0; i < dd_params_.size(); ++i) {
        dd_params_[i].name = dd_param_names_[i].c_str();
        dd_params_[i].group = dd_group_strings_[i].empty()
            ? nullptr : dd_group_strings_[i].c_str();
        if (!dd_choice_ptrs_[i].empty()) {
            for (size_t j = 0; j < dd_choice_labels_[i].size(); ++j)
                dd_choice_ptrs_[i][j] = dd_choice_labels_[i][j].c_str();
            dd_params_[i].choice_labels = dd_choice_ptrs_[i].data();
        }
    }
    for (size_t i = 0; i < dd_ports_.size(); ++i)
        dd_ports_[i].name = dd_port_names_[i].c_str();
    dd_desc_.params = dd_params_.data();
    dd_desc_.ports = dd_ports_.data();
}

void OperatorLoader::unload() {
    if (handle_) {
        if (dlclose(handle_) != 0) {
            const char* dl_err = dlerror();
            std::fprintf(stderr, "[vivid] dlclose failed: %s\n", dl_err ? dl_err : "unknown error");
        }
        handle_           = nullptr;
        desc_fn_          = nullptr;
        create_fn_        = nullptr;
        destroy_fn_       = nullptr;
        process_fn_       = nullptr;
        process_audio_fn_ = nullptr;
        process_gpu_fn_   = nullptr;
        draw_thumb_fn_    = nullptr;
        main_update_fn_   = nullptr;
    }
    if (dd_config_) {
        dd_config_.reset();
        dd_name_.clear();
        dd_param_names_.clear();
        dd_group_strings_.clear();
        dd_choice_labels_.clear();
        dd_choice_ptrs_.clear();
        dd_params_.clear();
        dd_port_names_.clear();
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
        if (instance) delete static_cast<DataDrivenFilter*>(instance);
        return;
    }
    if (destroy_fn_ && instance) {
        destroy_fn_(instance);
    }
}

void OperatorLoader::process(void* instance, VividProcessContext* ctx) const {
    if (process_fn_ && instance) {
        process_fn_(instance, ctx);
    }
}

void OperatorLoader::process_audio(void* instance, VividAudioContext* ctx) const {
    if (process_audio_fn_ && instance) {
        process_audio_fn_(instance, ctx);
    }
}

void OperatorLoader::process_gpu(void* instance, VividGpuContext* ctx) const {
    if (dd_config_) {
        if (!instance) return;
        static_cast<WgslFilterBase*>(instance)->process_gpu(ctx);
        return;
    }
    if (process_gpu_fn_ && instance) {
        process_gpu_fn_(instance, ctx);
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
