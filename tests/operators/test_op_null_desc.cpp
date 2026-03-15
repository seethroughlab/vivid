#include "operator_api/types.h"

extern "C" uint32_t vivid_abi_version() {
    return VIVID_OPERATOR_ABI_VERSION;
}

extern "C" const VividOperatorDescriptor* vivid_descriptor() {
    return nullptr;
}

extern "C" void* vivid_create() {
    return nullptr;
}

extern "C" void vivid_destroy(void*) {
}

extern "C" void vivid_process(void*, VividProcessContext*) {
}
