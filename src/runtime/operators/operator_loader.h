#pragma once

#include "operator_api/types.h"
#include <memory>
#include <string>
#include <vector>

namespace vivid {

struct WgslOperatorConfig;

class OperatorLoader {
public:
    struct LastError {
        std::string code;
        std::string message;
    };

    OperatorLoader() = default;
    ~OperatorLoader();

    // Non-copyable, move-only
    OperatorLoader(const OperatorLoader&) = delete;
    OperatorLoader& operator=(const OperatorLoader&) = delete;
    OperatorLoader(OperatorLoader&& other) noexcept;
    OperatorLoader& operator=(OperatorLoader&& other) noexcept;

    bool load(const char* path);
    void init_builtin(VividDescriptorFn, VividCreateFn, VividDestroyFn, VividProcessFrameFn);
    void init_wgsl_operator(std::shared_ptr<WgslOperatorConfig> config);
    void unload();

    const VividOperatorDescriptor* descriptor() const;
    void* create_instance() const;
    void  destroy_instance(void* instance) const;

    // Per-environment dispatch
    void  process_frame(void* instance, VividFrameContext* ctx) const;
    void  process_audio(void* instance, VividAudioContext* ctx) const;
    void  process_gpu(void* instance, VividGpuContext* ctx) const;

    bool has_draw_thumbnail() const { return draw_thumb_fn_ != nullptr; }
    void draw_thumbnail(void* instance, const VividThumbnailContext* ctx) const;

    bool has_file_drop_handlers() const { return file_drop_fn_ != nullptr; }
    const VividFileDropHandlerDescriptor* file_drop_handlers(uint32_t* count) const;

    bool has_draw_inspector() const { return draw_insp_fn_ != nullptr; }
    uint32_t inspector_mode() const;
    void draw_inspector(void* instance, VividInspectorContext* ctx) const;

    bool has_main_thread_update() const { return main_update_fn_ != nullptr; }
    void main_thread_update(void* instance, double time,
                            const char** file_param_values, uint32_t file_param_count) const;

    bool has_prepare_instance_assets() const { return prepare_assets_fn_ != nullptr; }
    void prepare_instance_assets(void* instance, const float* param_values,
                                 const char** file_param_values,
                                 uint32_t file_param_count) const;

    bool is_loaded() const { return handle_ != nullptr || desc_fn_ != nullptr || dd_config_ != nullptr; }
    bool is_shader_operator() const { return dd_config_ != nullptr; }
    const LastError& last_error() const { return last_error_; }

private:
    void set_last_error(std::string code, std::string message);
    void clear_last_error();

    void*                  handle_             = nullptr;
    VividDescriptorFn      desc_fn_            = nullptr;
    VividCreateFn          create_fn_          = nullptr;
    VividDestroyFn         destroy_fn_         = nullptr;
    VividProcessFrameFn         process_frame_fn_         = nullptr;
    VividProcessAudioFn    process_audio_fn_   = nullptr;
    VividProcessGpuFn      process_gpu_fn_     = nullptr;
    VividDrawThumbnailFn    draw_thumb_fn_     = nullptr;
    VividFileDropDescriptorFn file_drop_fn_    = nullptr;
    VividMainThreadUpdateFn main_update_fn_    = nullptr;
    VividPrepareInstanceAssetsFn prepare_assets_fn_ = nullptr;
    VividDrawInspectorFn   draw_insp_fn_      = nullptr;
    VividInspectorModeFn   insp_mode_fn_      = nullptr;
    void fixup_dd_pointers();  // re-point all C string pointers after move

    // Shader-backed operator support
    std::shared_ptr<WgslOperatorConfig> dd_config_;
    // Owned descriptor + owned string/param storage for stable const char* pointers
    std::string dd_name_;
    std::vector<std::string> dd_param_names_;
    std::vector<std::string> dd_group_strings_;
    std::vector<std::vector<std::string>> dd_choice_labels_;
    std::vector<std::vector<const char*>> dd_choice_ptrs_;
    std::vector<VividParamDescriptor> dd_params_;
    std::vector<std::string> dd_port_names_;
    std::vector<VividPortDescriptor> dd_ports_;
    VividOperatorDescriptor dd_desc_{};
    LastError last_error_{};
};

} // namespace vivid
