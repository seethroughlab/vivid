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
    void unload();

    const VividOperatorDescriptor* descriptor() const;
    void* create_instance() const;
    void  destroy_instance(void* instance) const;
    void  process(void* instance, const VividProcessContext* ctx) const;

    bool is_loaded() const { return handle_ != nullptr; }

private:
    void*             handle_     = nullptr;
    VividDescriptorFn desc_fn_    = nullptr;
    VividCreateFn     create_fn_  = nullptr;
    VividDestroyFn    destroy_fn_ = nullptr;
    VividProcessFn    process_fn_ = nullptr;
};

} // namespace vivid

#endif // VIVID_RUNTIME_OPERATOR_LOADER_H
