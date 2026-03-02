#pragma once
#include "operator_api/gpu_operator.h"

namespace vivid {

// Namespaced alias for vivid_sv() (defined in gpu_operator.h).
// Runtime/UI code uses vivid::to_sv(); operators use the global vivid_sv().
inline WGPUStringView to_sv(const char* s) { return vivid_sv(s); }

} // namespace vivid
