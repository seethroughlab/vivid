#ifndef VIVID_RUNTIME_OPERATOR_LOADER_H
#define VIVID_RUNTIME_OPERATOR_LOADER_H

#include "operator_api/types.h"

namespace vivid {

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
    void unload();

    const VividOperatorDescriptor* descriptor() const;
    void* create_instance() const;
    void  destroy_instance(void* instance) const;
    void  process(void* instance, const VividProcessContext* ctx) const;

    bool has_draw_thumbnail() const { return draw_thumb_fn_ != nullptr; }
    void draw_thumbnail(void* instance, const VividThumbnailContext* ctx) const;

    bool is_loaded() const { return handle_ != nullptr || desc_fn_ != nullptr; }

private:
    void*                  handle_         = nullptr;
    VividDescriptorFn      desc_fn_        = nullptr;
    VividCreateFn          create_fn_      = nullptr;
    VividDestroyFn         destroy_fn_     = nullptr;
    VividProcessFn         process_fn_     = nullptr;
    VividDrawThumbnailFn   draw_thumb_fn_  = nullptr;
};

} // namespace vivid

#endif // VIVID_RUNTIME_OPERATOR_LOADER_H
