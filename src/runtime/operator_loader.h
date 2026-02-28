#pragma once

#include "operator_api/types.h"
#include <memory>
#include <string>
#include <vector>

namespace vivid {

struct DataDrivenFilterConfig;

class OperatorLoader {
public:
    OperatorLoader() = default;
    ~OperatorLoader();

    // Non-copyable, move-only
    OperatorLoader(const OperatorLoader&) = delete;
    OperatorLoader& operator=(const OperatorLoader&) = delete;
    OperatorLoader(OperatorLoader&& other) noexcept;
    OperatorLoader& operator=(OperatorLoader&& other) noexcept;

    bool load(const char* path);
    void init_builtin(VividDescriptorFn, VividCreateFn, VividDestroyFn, VividProcessFn);
    void init_data_driven(std::shared_ptr<DataDrivenFilterConfig> config);
    void unload();

    const VividOperatorDescriptor* descriptor() const;
    void* create_instance() const;
    void  destroy_instance(void* instance) const;
    void  process(void* instance, const VividProcessContext* ctx) const;

    bool has_draw_thumbnail() const { return draw_thumb_fn_ != nullptr; }
    void draw_thumbnail(void* instance, const VividThumbnailContext* ctx) const;

    bool has_main_thread_update() const { return main_update_fn_ != nullptr; }
    void main_thread_update(void* instance, double time,
                            const char** file_param_values, uint32_t file_param_count) const;

    bool is_loaded() const { return handle_ != nullptr || desc_fn_ != nullptr || dd_config_ != nullptr; }
    bool is_data_driven() const { return dd_config_ != nullptr; }

private:
    void*                  handle_         = nullptr;
    VividDescriptorFn      desc_fn_        = nullptr;
    VividCreateFn          create_fn_      = nullptr;
    VividDestroyFn         destroy_fn_     = nullptr;
    VividProcessFn         process_fn_     = nullptr;
    VividDrawThumbnailFn   draw_thumb_fn_  = nullptr;
    VividMainThreadUpdateFn main_update_fn_ = nullptr;

    void fixup_dd_pointers();  // re-point all C string pointers after move

    // Data-driven filter support
    std::shared_ptr<DataDrivenFilterConfig> dd_config_;
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
};

} // namespace vivid
