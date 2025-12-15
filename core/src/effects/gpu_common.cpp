// Vivid Effects - GPU Common Utilities Implementation

#include <vivid/effects/gpu_common.h>
#include <unordered_map>

namespace vivid::effects::gpu {

// =============================================================================
// Sampler Cache
// =============================================================================

// Cache key combines device pointer and sampler type
struct SamplerKey {
    WGPUDevice device;
    int type;  // 0=linear_clamp, 1=nearest_clamp, 2=linear_repeat

    bool operator==(const SamplerKey& other) const {
        return device == other.device && type == other.type;
    }
};

struct SamplerKeyHash {
    size_t operator()(const SamplerKey& k) const {
        return std::hash<void*>()(k.device) ^ (std::hash<int>()(k.type) << 1);
    }
};

static std::unordered_map<SamplerKey, WGPUSampler, SamplerKeyHash> s_samplerCache;

static WGPUSampler createSampler(WGPUDevice device, WGPUFilterMode filter, WGPUAddressMode addressMode) {
    WGPUSamplerDescriptor desc = {};
    desc.addressModeU = addressMode;
    desc.addressModeV = addressMode;
    desc.addressModeW = addressMode;
    desc.magFilter = filter;
    desc.minFilter = filter;
    desc.mipmapFilter = (filter == WGPUFilterMode_Linear)
        ? WGPUMipmapFilterMode_Linear
        : WGPUMipmapFilterMode_Nearest;
    desc.maxAnisotropy = 1;
    return wgpuDeviceCreateSampler(device, &desc);
}

WGPUSampler getLinearClampSampler(WGPUDevice device) {
    SamplerKey key{device, 0};
    auto it = s_samplerCache.find(key);
    if (it != s_samplerCache.end()) {
        return it->second;
    }
    WGPUSampler sampler = createSampler(device, WGPUFilterMode_Linear, WGPUAddressMode_ClampToEdge);
    s_samplerCache[key] = sampler;
    return sampler;
}

WGPUSampler getNearestClampSampler(WGPUDevice device) {
    SamplerKey key{device, 1};
    auto it = s_samplerCache.find(key);
    if (it != s_samplerCache.end()) {
        return it->second;
    }
    WGPUSampler sampler = createSampler(device, WGPUFilterMode_Nearest, WGPUAddressMode_ClampToEdge);
    s_samplerCache[key] = sampler;
    return sampler;
}

WGPUSampler getLinearRepeatSampler(WGPUDevice device) {
    SamplerKey key{device, 2};
    auto it = s_samplerCache.find(key);
    if (it != s_samplerCache.end()) {
        return it->second;
    }
    WGPUSampler sampler = createSampler(device, WGPUFilterMode_Linear, WGPUAddressMode_Repeat);
    s_samplerCache[key] = sampler;
    return sampler;
}

} // namespace vivid::effects::gpu
