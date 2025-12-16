// Vivid Effects - Simple Texture Effect Template
// CRTP base class for simple single-input effects with uniform buffer

#pragma once

#include <vivid/effects/texture_operator.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <vivid/context.h>
#include <string>

namespace vivid::effects {

/**
 * @brief CRTP base class for simple single-input texture effects
 *
 * This template provides standard init/process/cleanup implementations
 * for effects that:
 * - Take a single texture input
 * - Have a single uniform buffer
 * - Use standard full-screen triangle rendering
 * - Use linear filtering sampler
 *
 * Derived classes must provide:
 * - fragmentShader() - returns the WGSL fragment shader source
 * - getUniforms() - returns the uniform struct values (via CRTP)
 *
 * @tparam Derived The derived class type (CRTP)
 * @tparam Uniforms The uniform buffer struct type
 *
 * Example usage:
 * @code
 * struct MyUniforms {
 *     float intensity;
 *     float _pad[3];
 * };
 *
 * class MyEffect : public SimpleTextureEffect<MyEffect, MyUniforms> {
 * public:
 *     Param<float> intensity{"intensity", 1.0f, 0.0f, 2.0f};
 *
 *     MyUniforms getUniforms() const {
 *         return {intensity, {0, 0, 0}};
 *     }
 *
 * protected:
 *     const char* fragmentShader() const override {
 *         return R"(
 *             struct Uniforms { intensity: f32, _pad1: f32, _pad2: f32, _pad3: f32 };
 *             @group(0) @binding(0) var<uniform> uniforms: Uniforms;
 *             @group(0) @binding(1) var inputTex: texture_2d<f32>;
 *             @group(0) @binding(2) var texSampler: sampler;
 *
 *             @fragment
 *             fn fs_main(input: VertexOutput) -> @location(0) vec4f {
 *                 let color = textureSample(inputTex, texSampler, input.uv);
 *                 return color * uniforms.intensity;
 *             }
 *         )";
 *     }
 * };
 * @endcode
 */
template<typename Derived, typename Uniforms>
class SimpleTextureEffect : public TextureOperator {
public:
    virtual ~SimpleTextureEffect();

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    /**
     * @brief Return the WGSL fragment shader source
     *
     * The shader should expect:
     * - @group(0) @binding(0) var<uniform> uniforms: YourUniformsType
     * - @group(0) @binding(1) var inputTex: texture_2d<f32>
     * - @group(0) @binding(2) var texSampler: sampler
     * - VertexOutput struct with position and uv fields (from gpu_common)
     */
    virtual const char* fragmentShader() const = 0;

    /**
     * @brief Optional: override to use a different sampler type
     * @return The sampler to use for texture sampling
     */
    virtual WGPUSampler getSampler(WGPUDevice device) {
        return gpu::getLinearClampSampler(device);
    }

protected:
    void createPipeline(Context& ctx);

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
    bool m_initialized = false;
};

// Out-of-class template definitions (non-inline)

template<typename Derived, typename Uniforms>
SimpleTextureEffect<Derived, Uniforms>::~SimpleTextureEffect() {
    cleanup();
}

template<typename Derived, typename Uniforms>
void SimpleTextureEffect<Derived, Uniforms>::init(Context& ctx) {
    if (m_initialized) return;
    createOutput(ctx);
    createPipeline(ctx);
    m_initialized = true;
}

template<typename Derived, typename Uniforms>
void SimpleTextureEffect<Derived, Uniforms>::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    // Match input resolution
    matchInputResolution(0);

    WGPUTextureView inView = inputView(0);
    if (!inView) return;

    if (!needsCook()) return;

    // Get uniforms from derived class via CRTP
    Uniforms uniforms = static_cast<Derived*>(this)->getUniforms();
    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group
    WGPUBindGroupEntry bindEntries[3] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(Uniforms);
    bindEntries[1].binding = 1;
    bindEntries[1].textureView = inView;
    bindEntries[2].binding = 2;
    bindEntries[2].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 3;
    bindDesc.entries = bindEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    // Execute render pass
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    endRenderPass(pass, encoder, ctx);

    wgpuBindGroupRelease(bindGroup);
    didCook();
}

template<typename Derived, typename Uniforms>
void SimpleTextureEffect<Derived, Uniforms>::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    // Note: m_sampler is managed by gpu_common cache, do not release
    m_sampler = nullptr;
    releaseOutput();
    m_initialized = false;
}

template<typename Derived, typename Uniforms>
void SimpleTextureEffect<Derived, Uniforms>::createPipeline(Context& ctx) {
    // Combine shared vertex shader with fragment shader from derived class
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) +
                               static_cast<Derived*>(this)->fragmentShader();

    // Use PipelineBuilder for pipeline creation
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(Uniforms))
           .texture(1)
           .sampler(2);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(Uniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Get sampler (can be overridden by derived class)
    m_sampler = getSampler(ctx.device());
}

/**
 * @brief Variant for generator effects (no input texture)
 *
 * For effects like SolidColor, Gradient, LFO that generate output
 * without requiring an input texture.
 *
 * @tparam Derived The derived class type (CRTP)
 * @tparam Uniforms The uniform buffer struct type
 */
template<typename Derived, typename Uniforms>
class SimpleGeneratorEffect : public TextureOperator {
public:
    virtual ~SimpleGeneratorEffect();

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    /**
     * @brief Return the WGSL fragment shader source
     *
     * The shader should expect:
     * - @group(0) @binding(0) var<uniform> uniforms: YourUniformsType
     * - VertexOutput struct with position and uv fields (from gpu_common)
     */
    virtual const char* fragmentShader() const = 0;

protected:
    void createPipeline(Context& ctx);

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    bool m_initialized = false;
};

// Out-of-class template definitions for SimpleGeneratorEffect (non-inline)

template<typename Derived, typename Uniforms>
SimpleGeneratorEffect<Derived, Uniforms>::~SimpleGeneratorEffect() {
    cleanup();
}

template<typename Derived, typename Uniforms>
void SimpleGeneratorEffect<Derived, Uniforms>::init(Context& ctx) {
    if (m_initialized) return;
    createOutput(ctx);
    createPipeline(ctx);
    m_initialized = true;
}

template<typename Derived, typename Uniforms>
void SimpleGeneratorEffect<Derived, Uniforms>::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    // Generators typically don't need to cook check, they always produce output

    // Get uniforms from derived class via CRTP
    Uniforms uniforms = static_cast<Derived*>(this)->getUniforms();
    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Execute render pass
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    endRenderPass(pass, encoder, ctx);

    didCook();
}

template<typename Derived, typename Uniforms>
void SimpleGeneratorEffect<Derived, Uniforms>::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroup);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    releaseOutput();
    m_initialized = false;
}

template<typename Derived, typename Uniforms>
void SimpleGeneratorEffect<Derived, Uniforms>::createPipeline(Context& ctx) {
    // Combine shared vertex shader with fragment shader from derived class
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) +
                               static_cast<Derived*>(this)->fragmentShader();

    // Use PipelineBuilder for pipeline creation
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(Uniforms));

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(Uniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Create bind group (uniform only)
    WGPUBindGroupEntry bindEntries[1] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(Uniforms);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = bindEntries;
    m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);
}

} // namespace vivid::effects
