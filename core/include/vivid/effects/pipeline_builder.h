// Vivid Effects - Pipeline Builder Utility
// Fluent API for creating render pipelines with less boilerplate

#pragma once

#include <webgpu/webgpu.h>
#include <vector>
#include <string>

namespace vivid::effects::gpu {

// Forward declaration
class PipelineBuilder;

// Binding types for the builder
enum class BindingType {
    Uniform,
    Texture,
    Sampler,
    SamplerNonFiltering,
    StorageBuffer,
    StorageTexture
};

struct BindingEntry {
    uint32_t binding;
    BindingType type;
    uint64_t size;  // For uniform/storage buffers
    WGPUShaderStage visibility;
};

// Pipeline builder with fluent interface
class PipelineBuilder {
public:
    explicit PipelineBuilder(WGPUDevice device);
    ~PipelineBuilder();

    // Shader configuration
    PipelineBuilder& shader(const char* wgslSource);
    PipelineBuilder& shader(const std::string& wgslSource);
    PipelineBuilder& vertexEntry(const char* entryPoint);
    PipelineBuilder& fragmentEntry(const char* entryPoint);

    // Output configuration
    PipelineBuilder& colorTarget(WGPUTextureFormat format);
    PipelineBuilder& colorTargetWithBlend(WGPUTextureFormat format);

    // Binding configuration - fragment stage by default
    PipelineBuilder& uniform(uint32_t binding, uint64_t size);
    PipelineBuilder& texture(uint32_t binding);
    PipelineBuilder& sampler(uint32_t binding, bool filtering = true);
    PipelineBuilder& storageBuffer(uint32_t binding, uint64_t size);

    // Binding configuration with explicit visibility
    PipelineBuilder& uniform(uint32_t binding, uint64_t size, WGPUShaderStage visibility);
    PipelineBuilder& texture(uint32_t binding, WGPUShaderStage visibility);
    PipelineBuilder& sampler(uint32_t binding, bool filtering, WGPUShaderStage visibility);

    // Build the pipeline
    WGPURenderPipeline build();

    // Access the bind group layout after build()
    WGPUBindGroupLayout bindGroupLayout() const { return m_bindGroupLayout; }

    // Check if build succeeded
    bool valid() const { return m_pipeline != nullptr; }

private:
    void createBindGroupLayout();
    void createPipelineLayout();
    void createShaderModule();

    WGPUDevice m_device;
    std::string m_shaderSource;
    std::string m_vertexEntry = "vs_main";
    std::string m_fragmentEntry = "fs_main";
    WGPUTextureFormat m_colorFormat = WGPUTextureFormat_RGBA16Float;
    bool m_useBlend = false;

    std::vector<BindingEntry> m_bindings;

    // Created resources
    WGPUShaderModule m_shaderModule = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUPipelineLayout m_pipelineLayout = nullptr;
    WGPURenderPipeline m_pipeline = nullptr;
};

// Pre-defined bind group layout helpers for common patterns
namespace BindGroupLayouts {
    // Single uniform buffer (binding 0)
    WGPUBindGroupLayout uniformOnly(WGPUDevice device, uint64_t uniformSize);

    // Uniform + texture + sampler (bindings 0, 1, 2)
    WGPUBindGroupLayout uniformTextureSampler(WGPUDevice device, uint64_t uniformSize);

    // Uniform + two textures + sampler (bindings 0, 1, 2, 3) - for composite ops
    WGPUBindGroupLayout uniformTwoTexturesSampler(WGPUDevice device, uint64_t uniformSize);
}

} // namespace vivid::effects::gpu
