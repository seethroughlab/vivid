// Vivid Effects - GPU Common Utilities Implementation

#include <vivid/effects/gpu_common.h>
#include <unordered_map>
#include <cmath>

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

// =============================================================================
// Blend State Factories
// =============================================================================

WGPUBlendState createAlphaBlendState() {
    WGPUBlendState state = {};
    state.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    state.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    state.color.operation = WGPUBlendOperation_Add;
    state.alpha.srcFactor = WGPUBlendFactor_One;
    state.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    state.alpha.operation = WGPUBlendOperation_Add;
    return state;
}

WGPUBlendState createAdditiveBlendState() {
    WGPUBlendState state = {};
    state.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    state.color.dstFactor = WGPUBlendFactor_One;
    state.color.operation = WGPUBlendOperation_Add;
    state.alpha.srcFactor = WGPUBlendFactor_One;
    state.alpha.dstFactor = WGPUBlendFactor_One;
    state.alpha.operation = WGPUBlendOperation_Add;
    return state;
}

// =============================================================================
// White Texture Factory
// =============================================================================

WhiteTexture createWhiteTexture(WGPUDevice device, WGPUQueue queue) {
    WhiteTexture result = {};

    WGPUTextureDescriptor texDesc = {};
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {1, 1, 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    result.texture = wgpuDeviceCreateTexture(device, &texDesc);

    uint8_t white[4] = {255, 255, 255, 255};
    WGPUTexelCopyTextureInfo dest = {};
    dest.texture = result.texture;
    dest.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = 4;
    layout.rowsPerImage = 1;
    WGPUExtent3D size = {1, 1, 1};
    wgpuQueueWriteTexture(queue, &dest, white, 4, &layout, &size);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    result.view = wgpuTextureCreateView(result.texture, &viewDesc);

    return result;
}

// =============================================================================
// Circle Mesh Generator
// =============================================================================

CircleMesh generateCircleMesh(int segments) {
    CircleMesh mesh;

    // Center vertex
    mesh.vertices.push_back(0.0f);
    mesh.vertices.push_back(0.0f);

    // Edge vertices
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159265f;
        mesh.vertices.push_back(std::cos(angle));
        mesh.vertices.push_back(std::sin(angle));
    }

    // Triangle fan indices
    for (int i = 0; i < segments; i++) {
        mesh.indices.push_back(0);
        mesh.indices.push_back(i + 1);
        mesh.indices.push_back(i + 2);
    }

    return mesh;
}

} // namespace vivid::effects::gpu
