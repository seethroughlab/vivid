#pragma once
// Minimal local replacement for classic's common/gpu_util.h.
// classic's version pulled in the whole operator API just for this helper;
// we only need const char* -> WGPUStringView (wgpu-native v29 string type).
#include <webgpu/webgpu.h>
#include <cstring>
#include <cstdint>

namespace vivid {
inline WGPUStringView to_sv(const char* s) {
    return WGPUStringView{ s, s ? std::strlen(s) : 0 };
}

// Frame anti-aliasing: the whole frame renders into a 4x multisampled color
// target that resolves to the swap-chain surface (see GpuContext). Every pipeline
// that draws into the frame view (renderer_2d, the present blit) must declare this
// sample count; offscreen operator render targets stay at 1x.
constexpr uint32_t kMsaaSamples = 4;
}  // namespace vivid
