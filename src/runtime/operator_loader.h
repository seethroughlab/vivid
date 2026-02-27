#ifndef VIVID_RUNTIME_OPERATOR_LOADER_H
#define VIVID_RUNTIME_OPERATOR_LOADER_H

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

    bool is_loaded() const { return handle_ != nullptr || desc_fn_ != nullptr || dd_config_ != nullptr; }
    bool is_data_driven() const { return dd_config_ != nullptr; }

private:
    void*                  handle_         = nullptr;
    VividDescriptorFn      desc_fn_        = nullptr;
    VividCreateFn          create_fn_      = nullptr;
    VividDestroyFn         destroy_fn_     = nullptr;
    VividProcessFn         process_fn_     = nullptr;
    VividDrawThumbnailFn   draw_thumb_fn_  = nullptr;

    // Data-driven filter support
    std::shared_ptr<DataDrivenFilterConfig> dd_config_;
    // Owned descriptor + owned string/param storage for stable const char* pointers
    std::string dd_name_;
    std::vector<std::string> dd_param_names_;
    std::vector<VividParamDescriptor> dd_params_;
    std::vector<VividPortDescriptor> dd_ports_;
    VividOperatorDescriptor dd_desc_{};
};

} // namespace vivid

#endif // VIVID_RUNTIME_OPERATOR_LOADER_H
