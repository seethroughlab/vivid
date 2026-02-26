#pragma once
#include <webgpu/webgpu.h>
#include <cstring>

namespace vivid {

inline WGPUStringView to_sv(const char* s) {
    return { s, s ? std::strlen(s) : 0 };
}

} // namespace vivid
