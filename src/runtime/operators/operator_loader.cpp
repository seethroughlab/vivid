#include "runtime/operators/operator_loader.h"
#include "operator_api/data_driven_filter.h"
#include "operator_api/port_type_registry.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace vivid {

namespace {
struct RetainedFailedPluginHandle {
    std::string path;
    void* handle = nullptr;
};

std::vector<RetainedFailedPluginHandle>& retained_failed_plugin_handles() {
    static std::vector<RetainedFailedPluginHandle> handles;
    return handles;
}

void retain_failed_plugin_handle(const char* path, void* handle) {
    if (!handle) return;
    retained_failed_plugin_handles().push_back({path ? path : "", handle});
}

bool hot_reload_param_layout_compatible(const VividOperatorDescriptor* old_desc,
                                        const VividOperatorDescriptor* new_desc) {
    if (!old_desc || !new_desc) return false;
    if (old_desc->param_count > new_desc->param_count) return false;
    for (uint32_t i = 0; i < old_desc->param_count; ++i) {
        const auto& old_param = old_desc->params[i];
        const auto& new_param = new_desc->params[i];
        if (!old_param.name || !new_param.name) return false;
        if (std::strcmp(old_param.name, new_param.name) != 0) return false;
        if (old_param.type != new_param.type) return false;
    }
    return true;
}

bool hot_reload_port_layout_compatible(const VividOperatorDescriptor* old_desc,
                                       const VividOperatorDescriptor* new_desc) {
    if (!old_desc || !new_desc) return false;
    if (old_desc->port_count != new_desc->port_count) return false;
    for (uint32_t i = 0; i < old_desc->port_count; ++i) {
        const auto& old_port = old_desc->ports[i];
        const auto& new_port = new_desc->ports[i];
        if (!old_port.name || !new_port.name) return false;
        if (std::strcmp(old_port.name, new_port.name) != 0) return false;
        if (old_port.type != new_port.type) return false;
        if (old_port.direction != new_port.direction) return false;
        if (old_port.transport != new_port.transport) return false;
        if (old_port.payload_size != new_port.payload_size) return false;
        if (old_port.channels != new_port.channels) return false;
        const char* old_stable = old_port.stable_type_id ? old_port.stable_type_id : "";
        const char* new_stable = new_port.stable_type_id ? new_port.stable_type_id : "";
        if (std::strcmp(old_stable, new_stable) != 0) return false;
    }
    return true;
}

bool hot_reload_descriptor_compatible(const VividOperatorDescriptor* old_desc,
                                      const VividOperatorDescriptor* new_desc) {
    if (!old_desc || !new_desc) return true;
    if (old_desc->has_process_gpu != new_desc->has_process_gpu) return false;
    if (!hot_reload_param_layout_compatible(old_desc, new_desc)) return false;
    if (!hot_reload_port_layout_compatible(old_desc, new_desc)) return false;
    if (old_desc->lane_behavior != new_desc->lane_behavior) return false;
    return true;
}
} // namespace

void OperatorLoader::set_last_error(std::string code, std::string message) {
    last_error_.code = std::move(code);
    last_error_.message = std::move(message);
}

void OperatorLoader::clear_last_error() {
    last_error_.code.clear();
    last_error_.message.clear();
}

OperatorLoader::~OperatorLoader() {
    unload();
}

OperatorLoader::OperatorLoader(OperatorLoader&& other) noexcept
    : handle_(other.handle_)
    , desc_fn_(other.desc_fn_)
    , create_fn_(other.create_fn_)
    , destroy_fn_(other.destroy_fn_)
    , process_frame_fn_(other.process_frame_fn_)
    , process_audio_fn_(other.process_audio_fn_)
    , process_gpu_fn_(other.process_gpu_fn_)
    , draw_thumb_fn_(other.draw_thumb_fn_)
    , file_drop_fn_(other.file_drop_fn_)
    , main_update_fn_(other.main_update_fn_)
    , prepare_assets_fn_(other.prepare_assets_fn_)
    , draw_insp_fn_(other.draw_insp_fn_)
    , insp_mode_fn_(other.insp_mode_fn_)
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
    other.process_frame_fn_       = nullptr;
    other.process_audio_fn_ = nullptr;
    other.process_gpu_fn_   = nullptr;
    other.draw_thumb_fn_ = nullptr;
    other.file_drop_fn_   = nullptr;
    other.main_update_fn_   = nullptr;
    other.prepare_assets_fn_ = nullptr;
    other.draw_insp_fn_     = nullptr;
    other.insp_mode_fn_     = nullptr;
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
        process_frame_fn_       = other.process_frame_fn_;
        process_audio_fn_ = other.process_audio_fn_;
        process_gpu_fn_   = other.process_gpu_fn_;
        draw_thumb_fn_ = other.draw_thumb_fn_;
        file_drop_fn_ = other.file_drop_fn_;
        main_update_fn_   = other.main_update_fn_;
        prepare_assets_fn_ = other.prepare_assets_fn_;
        draw_insp_fn_     = other.draw_insp_fn_;
        insp_mode_fn_     = other.insp_mode_fn_;
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
        other.process_frame_fn_       = nullptr;
        other.process_audio_fn_ = nullptr;
        other.process_gpu_fn_   = nullptr;
        other.draw_thumb_fn_ = nullptr;
        other.file_drop_fn_ = nullptr;
        other.main_update_fn_   = nullptr;
        other.prepare_assets_fn_ = nullptr;
        other.draw_insp_fn_     = nullptr;
        other.insp_mode_fn_     = nullptr;
        other.dd_desc_ = {};
        // Fixup descriptor pointers to our own storage
        if (dd_config_) {
            fixup_dd_pointers();
        }
    }
    return *this;
}

bool OperatorLoader::load(const char* path) {
    clear_last_error();

    // Attempt to open the new dylib before touching current state (atomic swap).
    void* new_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!new_handle) {
        const char* dl_err = dlerror();
        std::string msg = dl_err ? dl_err : "unknown error";
        set_last_error("dlopen_failed", msg);
        std::fprintf(stderr, "[vivid] dlopen failed: %s\n", msg.c_str());
        return false;  // Old dylib still live
    }

    // Resolve required symbols from new handle
    auto abi_fn = reinterpret_cast<VividAbiVersionFn>(dlsym(new_handle, "vivid_abi_version"));
    if (!abi_fn) {
        set_last_error("missing_abi_symbol",
                       "missing symbol: vivid_abi_version");
        std::fprintf(stderr, "[vivid] Missing symbol: vivid_abi_version (stale/incompatible plugin)\n");
        dlclose(new_handle);
        return false;
    }
    const uint32_t abi = abi_fn();
    if (abi != VIVID_OPERATOR_ABI_VERSION) {
        set_last_error("abi_mismatch",
                       "plugin ABI " + std::to_string(abi) +
                       " does not match runtime ABI " + std::to_string(VIVID_OPERATOR_ABI_VERSION));
        std::fprintf(stderr, "[vivid] Incompatible plugin ABI: got %u, expected %u\n",
                     abi, VIVID_OPERATOR_ABI_VERSION);
        dlclose(new_handle);
        return false;
    }

    auto new_desc_fn    = reinterpret_cast<VividDescriptorFn>(dlsym(new_handle, "vivid_descriptor"));
    auto new_create_fn  = reinterpret_cast<VividCreateFn>(dlsym(new_handle, "vivid_create"));
    auto new_destroy_fn = reinterpret_cast<VividDestroyFn>(dlsym(new_handle, "vivid_destroy"));

    // Per-environment process entry points
    auto new_process_fn       = reinterpret_cast<VividProcessFrameFn>(dlsym(new_handle, "vivid_process_frame"));
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
        set_last_error("missing_required_symbols",
                       "plugin is missing one or more required Vivid entry points");
        dlclose(new_handle);
        return false;  // Old dylib still live
    }

    const VividOperatorDescriptor* current_desc = descriptor();
    const VividOperatorDescriptor* new_desc = new_desc_fn();
    if (!new_desc) {
        set_last_error("null_descriptor",
                       std::string("vivid_descriptor returned null for ") + path);
        std::fprintf(stderr, "[vivid] vivid_descriptor returned null for %s\n", path);
        dlclose(new_handle);
        return false;
    }
    if (!new_desc->name || !*new_desc->name) {
        set_last_error("invalid_descriptor_name",
                       std::string("vivid_descriptor returned missing/empty name for ") + path);
        std::fprintf(stderr, "[vivid] vivid_descriptor returned missing/empty name for %s\n", path);
        dlclose(new_handle);
        return false;
    }
    if (!hot_reload_descriptor_compatible(current_desc, new_desc)) {
        set_last_error("hot_reload_incompatible_descriptor",
                       "descriptor shape changed incompatibly; rebuild graph/runtime instead");
        std::fprintf(stderr,
                     "[vivid] Hot-reload rejected: descriptor shape changed incompatibly; "
                     "rebuild graph/runtime instead.\n");
        dlclose(new_handle);
        return false;
    }

    // Pre-register custom port types before committing the loader swap. This
    // preserves the previous loader on registration failure.
    if (auto* describe_types = reinterpret_cast<VividDescribeCustomTypesFn>(
            dlsym(new_handle, "vivid_describe_custom_types"))) {
        uint32_t count = 0;
        const VividPortTypeInfo* infos = describe_types(&count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!vivid_register_port_type(&infos[i])) {
                std::string stable_id =
                    (infos && infos[i].stable_type_id) ? infos[i].stable_type_id : "<unknown>";
                set_last_error("custom_type_registration_failed",
                               "failed to register custom port type '" + stable_id + "'");
                std::fprintf(stderr, "[vivid] Failed to register custom port type for plugin: %s\n", path);
                // Some malformed plugins have unsafe teardown paths during dlclose().
                // Retain the failed handle and keep the current live loader unchanged.
                retain_failed_plugin_handle(path, new_handle);
                return false;
            }
        }
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
    process_frame_fn_       = new_process_fn;
    process_audio_fn_ = new_process_audio_fn;
    process_gpu_fn_   = new_process_gpu_fn;

    // Optional entry points
    draw_thumb_fn_ =
        reinterpret_cast<VividDrawThumbnailFn>(dlsym(new_handle, "vivid_draw_thumbnail"));
    file_drop_fn_ =
        reinterpret_cast<VividFileDropDescriptorFn>(dlsym(new_handle, "vivid_file_drop_descriptor"));
    main_update_fn_ = reinterpret_cast<VividMainThreadUpdateFn>(dlsym(new_handle, "vivid_main_thread_update"));
    prepare_assets_fn_ = reinterpret_cast<VividPrepareInstanceAssetsFn>(
        dlsym(new_handle, "vivid_prepare_instance_assets"));
    draw_insp_fn_   = reinterpret_cast<VividDrawInspectorFn>(dlsym(new_handle, "vivid_draw_inspector"));
    insp_mode_fn_   = reinterpret_cast<VividInspectorModeFn>(dlsym(new_handle, "vivid_inspector_mode"));

    clear_last_error();
    return true;
}

void OperatorLoader::init_builtin(VividDescriptorFn desc, VividCreateFn create,
                                   VividDestroyFn destroy, VividProcessFrameFn process) {
    unload();
    desc_fn_    = desc;
    create_fn_  = create;
    destroy_fn_ = destroy;
    process_frame_fn_ = process;
}

void OperatorLoader::init_wgsl_operator(std::shared_ptr<WgslOperatorConfig> config) {
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
        dp.description = nullptr;
        dp.asset_kind = sp.asset_kind.empty() ? nullptr : sp.asset_kind.c_str();

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

    dd_desc_.param_count = static_cast<uint32_t>(dd_params_.size());
    dd_desc_.port_count = static_cast<uint32_t>(dd_ports_.size());
    dd_desc_.time_dependent = dd_config_->time_dependent ? 1 : 0;
    dd_desc_.has_process_audio = 0;
    dd_desc_.has_process_gpu = 1;
    dd_desc_.has_process_frame = 0;
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
        process_frame_fn_       = nullptr;
        process_audio_fn_ = nullptr;
        process_gpu_fn_   = nullptr;
        draw_thumb_fn_ = nullptr;
        file_drop_fn_ = nullptr;
        main_update_fn_   = nullptr;
        prepare_assets_fn_ = nullptr;
        draw_insp_fn_     = nullptr;
        insp_mode_fn_     = nullptr;
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
    if (dd_config_) return new WgslOperator(dd_config_);
    return create_fn_ ? create_fn_() : nullptr;
}

void OperatorLoader::destroy_instance(void* instance) const {
    if (dd_config_) {
        if (instance) delete static_cast<WgslOperator*>(instance);
        return;
    }
    if (destroy_fn_ && instance) {
        destroy_fn_(instance);
    }
}

void OperatorLoader::process_frame(void* instance, VividFrameContext* ctx) const {
    if (process_frame_fn_ && instance) {
        process_frame_fn_(instance, ctx);
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

const VividFileDropHandlerDescriptor* OperatorLoader::file_drop_handlers(uint32_t* count) const {
    if (count) *count = 0;
    return file_drop_fn_ ? file_drop_fn_(count) : nullptr;
}

void OperatorLoader::main_thread_update(void* instance, double time,
                                         const char** file_param_values,
                                         uint32_t file_param_count) const {
    if (main_update_fn_ && instance) {
        main_update_fn_(instance, time, file_param_values, file_param_count);
    }
}

void OperatorLoader::prepare_instance_assets(void* instance,
                                             const float* param_values,
                                             const char** file_param_values,
                                             uint32_t file_param_count) const {
    if (prepare_assets_fn_ && instance) {
        prepare_assets_fn_(instance, param_values, file_param_values, file_param_count);
    }
}

uint32_t OperatorLoader::inspector_mode() const {
    return insp_mode_fn_ ? insp_mode_fn_() : VIVID_INSPECTOR_STANDARD;
}

void OperatorLoader::draw_inspector(void* instance, VividInspectorContext* ctx) const {
    if (draw_insp_fn_ && instance) {
        draw_insp_fn_(instance, ctx);
    }
}

} // namespace vivid
