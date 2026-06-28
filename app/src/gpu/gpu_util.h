#pragma once
// Minimal local replacement for classic's common/gpu_util.h.
// classic's version pulled in the whole operator API just for this helper;
// we only need const char* -> WGPUStringView (wgpu-native v29 string type).
#include <webgpu/webgpu.h>
#include <cstring>

namespace vivid {
inline WGPUStringView to_sv(const char* s) {
    return WGPUStringView{ s, s ? std::strlen(s) : 0 };
}
}  // namespace vivid
