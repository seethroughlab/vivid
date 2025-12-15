// Vivid Effects - Pipeline Builder Implementation

#include <vivid/effects/pipeline_builder.h>
#include <vivid/effects/gpu_common.h>

namespace vivid::effects::gpu {

PipelineBuilder::PipelineBuilder(WGPUDevice device)
    : m_device(device) {}

PipelineBuilder::~PipelineBuilder() {
    // Note: We don't release m_bindGroupLayout here because it's returned to the caller
    // The caller is responsible for releasing it
    if (m_shaderModule) {
        wgpuShaderModuleRelease(m_shaderModule);
    }
    if (m_pipelineLayout) {
        wgpuPipelineLayoutRelease(m_pipelineLayout);
    }
    // m_pipeline is returned to caller, don't release
}

PipelineBuilder& PipelineBuilder::shader(const char* wgslSource) {
    m_shaderSource = wgslSource;
    return *this;
}

PipelineBuilder& PipelineBuilder::shader(const std::string& wgslSource) {
    m_shaderSource = wgslSource;
    return *this;
}

PipelineBuilder& PipelineBuilder::vertexEntry(const char* entryPoint) {
    m_vertexEntry = entryPoint;
    return *this;
}

PipelineBuilder& PipelineBuilder::fragmentEntry(const char* entryPoint) {
    m_fragmentEntry = entryPoint;
    return *this;
}

PipelineBuilder& PipelineBuilder::colorTarget(WGPUTextureFormat format) {
    m_colorFormat = format;
    m_useBlend = false;
    return *this;
}

PipelineBuilder& PipelineBuilder::colorTargetWithBlend(WGPUTextureFormat format) {
    m_colorFormat = format;
    m_useBlend = true;
    return *this;
}

PipelineBuilder& PipelineBuilder::uniform(uint32_t binding, uint64_t size) {
    return uniform(binding, size, WGPUShaderStage_Fragment);
}

PipelineBuilder& PipelineBuilder::uniform(uint32_t binding, uint64_t size, WGPUShaderStage visibility) {
    m_bindings.push_back({binding, BindingType::Uniform, size, visibility});
    return *this;
}

PipelineBuilder& PipelineBuilder::texture(uint32_t binding) {
    return texture(binding, WGPUShaderStage_Fragment);
}

PipelineBuilder& PipelineBuilder::texture(uint32_t binding, WGPUShaderStage visibility) {
    m_bindings.push_back({binding, BindingType::Texture, 0, visibility});
    return *this;
}

PipelineBuilder& PipelineBuilder::sampler(uint32_t binding, bool filtering) {
    return sampler(binding, filtering, WGPUShaderStage_Fragment);
}

PipelineBuilder& PipelineBuilder::sampler(uint32_t binding, bool filtering, WGPUShaderStage visibility) {
    auto type = filtering ? BindingType::Sampler : BindingType::SamplerNonFiltering;
    m_bindings.push_back({binding, type, 0, visibility});
    return *this;
}

PipelineBuilder& PipelineBuilder::storageBuffer(uint32_t binding, uint64_t size) {
    m_bindings.push_back({binding, BindingType::StorageBuffer, size, WGPUShaderStage_Fragment});
    return *this;
}

void PipelineBuilder::createShaderModule() {
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(m_shaderSource.c_str());

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    m_shaderModule = wgpuDeviceCreateShaderModule(m_device, &shaderDesc);
}

void PipelineBuilder::createBindGroupLayout() {
    std::vector<WGPUBindGroupLayoutEntry> entries(m_bindings.size());

    for (size_t i = 0; i < m_bindings.size(); ++i) {
        auto& entry = entries[i];
        auto& binding = m_bindings[i];

        entry = {};
        entry.binding = binding.binding;
        entry.visibility = binding.visibility;

        switch (binding.type) {
            case BindingType::Uniform:
                entry.buffer.type = WGPUBufferBindingType_Uniform;
                entry.buffer.minBindingSize = binding.size;
                break;
            case BindingType::Texture:
                entry.texture.sampleType = WGPUTextureSampleType_Float;
                entry.texture.viewDimension = WGPUTextureViewDimension_2D;
                break;
            case BindingType::Sampler:
                entry.sampler.type = WGPUSamplerBindingType_Filtering;
                break;
            case BindingType::SamplerNonFiltering:
                entry.sampler.type = WGPUSamplerBindingType_NonFiltering;
                break;
            case BindingType::StorageBuffer:
                entry.buffer.type = WGPUBufferBindingType_Storage;
                entry.buffer.minBindingSize = binding.size;
                break;
            case BindingType::StorageTexture:
                entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
                entry.storageTexture.format = WGPUTextureFormat_RGBA16Float;
                entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                break;
        }
    }

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = entries.size();
    layoutDesc.entries = entries.data();
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_device, &layoutDesc);
}

void PipelineBuilder::createPipelineLayout() {
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    m_pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);
}

WGPURenderPipeline PipelineBuilder::build() {
    createShaderModule();
    createBindGroupLayout();
    createPipelineLayout();

    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = m_colorFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    if (m_useBlend) {
        colorTarget.blend = &blendState;
    }

    WGPUFragmentState fragmentState = {};
    fragmentState.module = m_shaderModule;
    fragmentState.entryPoint = toStringView(m_fragmentEntry.c_str());
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = m_pipelineLayout;
    pipelineDesc.vertex.module = m_shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView(m_vertexEntry.c_str());
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_pipeline = wgpuDeviceCreateRenderPipeline(m_device, &pipelineDesc);
    return m_pipeline;
}

// Pre-defined bind group layout helpers
namespace BindGroupLayouts {

WGPUBindGroupLayout uniformOnly(WGPUDevice device, uint64_t uniformSize) {
    WGPUBindGroupLayoutEntry entries[1] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = uniformSize;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = entries;
    return wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
}

WGPUBindGroupLayout uniformTextureSampler(WGPUDevice device, uint64_t uniformSize) {
    WGPUBindGroupLayoutEntry entries[3] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = uniformSize;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 3;
    layoutDesc.entries = entries;
    return wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
}

WGPUBindGroupLayout uniformTwoTexturesSampler(WGPUDevice device, uint64_t uniformSize) {
    WGPUBindGroupLayoutEntry entries[4] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = uniformSize;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Fragment;
    entries[3].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 4;
    layoutDesc.entries = entries;
    return wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
}

} // namespace BindGroupLayouts

} // namespace vivid::effects::gpu
