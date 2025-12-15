// Vivid Effects 2D - FrameCache Operator Implementation

#include <vivid/effects/frame_cache.h>
#include <vivid/context.h>
#include <cstring>

namespace vivid::effects {

// Simple blit shader for format conversion
static const char* s_blitShader = R"(
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

@group(0) @binding(0) var inputTexture: texture_2d<f32>;
@group(0) @binding(1) var texSampler: sampler;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f(3.0, -1.0),
        vec2f(-1.0, 3.0)
    );
    var output: VertexOutput;
    output.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    output.uv = (positions[vertexIndex] + 1.0) * 0.5;
    output.uv.y = 1.0 - output.uv.y;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return textureSample(inputTexture, texSampler, input.uv);
}
)";

FrameCache::~FrameCache() {
    cleanup();
}

void FrameCache::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createCacheTexture(ctx);
    createBlitPipeline(ctx);

    m_initialized = true;
}

void FrameCache::createCacheTexture(Context& ctx) {
    int numFrames = static_cast<int>(frameCount);
    m_allocatedFrames = numFrames;

    // Create 2D texture array for frame cache
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("FrameCache Array");
    texDesc.size = {
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        static_cast<uint32_t>(numFrames)  // depthOrArrayLayers = frame count
    };
    texDesc.format = EFFECTS_FORMAT;
    texDesc.usage = WGPUTextureUsage_TextureBinding |
                    WGPUTextureUsage_CopyDst |
                    WGPUTextureUsage_RenderAttachment;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;  // 2D array uses 2D dimension

    m_cacheTexture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    // Create full array view for sampling
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = EFFECTS_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2DArray;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = static_cast<uint32_t>(numFrames);

    m_cacheView = wgpuTextureCreateView(m_cacheTexture, &viewDesc);

    // Create per-layer views for rendering
    m_layerViews.resize(numFrames);
    for (int i = 0; i < numFrames; i++) {
        WGPUTextureViewDescriptor layerViewDesc = {};
        layerViewDesc.format = EFFECTS_FORMAT;
        layerViewDesc.dimension = WGPUTextureViewDimension_2D;
        layerViewDesc.baseMipLevel = 0;
        layerViewDesc.mipLevelCount = 1;
        layerViewDesc.baseArrayLayer = static_cast<uint32_t>(i);
        layerViewDesc.arrayLayerCount = 1;

        m_layerViews[i] = wgpuTextureCreateView(m_cacheTexture, &layerViewDesc);
    }

    m_writeIndex = 0;
    m_framesWritten = 0;
}

void FrameCache::createBlitPipeline(Context& ctx) {
    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(s_blitShader);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("FrameCache Blit Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(ctx.device(), &samplerDesc);

    // Create bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[2] = {};

    // Input texture
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Sampler
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.label = toStringView("FrameCache Bind Group Layout");
    layoutDesc.entryCount = 2;
    layoutDesc.entries = layoutEntries;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device(), &layoutDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pipelineLayoutDesc);

    // Create render pipeline
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = toStringView("FrameCache Blit Pipeline");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_blitPipeline = wgpuDeviceCreateRenderPipeline(ctx.device(), &pipelineDesc);

    // Cleanup
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void FrameCache::recreateCacheIfNeeded(Context& ctx) {
    int requestedFrames = static_cast<int>(frameCount);
    if (requestedFrames != m_allocatedFrames) {
        // Release old cache
        for (auto& view : m_layerViews) {
            if (view) wgpuTextureViewRelease(view);
        }
        m_layerViews.clear();

        if (m_cacheView) {
            wgpuTextureViewRelease(m_cacheView);
            m_cacheView = nullptr;
        }
        if (m_cacheTexture) {
            wgpuTextureRelease(m_cacheTexture);
            m_cacheTexture = nullptr;
        }

        // Create new cache
        createCacheTexture(ctx);
    }
}

void FrameCache::blitToTarget(Context& ctx, WGPUTextureView srcView, WGPUTextureView dstView) {
    // Create bind group for this blit operation
    WGPUBindGroupEntry bindEntries[2] = {};

    bindEntries[0].binding = 0;
    bindEntries[0].textureView = srcView;

    bindEntries[1].binding = 1;
    bindEntries[1].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.label = toStringView("FrameCache Blit Bind Group");
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 2;
    bindDesc.entries = bindEntries;

    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

    // Create render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = dstView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    // Draw
    wgpuRenderPassEncoderSetPipeline(pass, m_blitPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Submit
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
}

void FrameCache::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    // Match input resolution
    matchInputResolution(0);

    // Check if frame count changed
    recreateCacheIfNeeded(ctx);

    // Get input texture view
    WGPUTextureView inView = inputView(0);
    if (!inView) {
        return;  // No input
    }

    // Blit input to current cache slot (handles format conversion)
    if (m_writeIndex < static_cast<int>(m_layerViews.size()) && m_layerViews[m_writeIndex]) {
        blitToTarget(ctx, inView, m_layerViews[m_writeIndex]);
    }

    // Advance write index (ring buffer)
    m_writeIndex = (m_writeIndex + 1) % m_allocatedFrames;
    m_framesWritten++;

    // Blit input to output (pass-through with format conversion)
    blitToTarget(ctx, inView, m_outputView);

    didCook();
}

void FrameCache::cleanup() {
    for (auto& view : m_layerViews) {
        if (view) wgpuTextureViewRelease(view);
    }
    m_layerViews.clear();

    if (m_cacheView) {
        wgpuTextureViewRelease(m_cacheView);
        m_cacheView = nullptr;
    }
    if (m_cacheTexture) {
        wgpuTextureRelease(m_cacheTexture);
        m_cacheTexture = nullptr;
    }
    if (m_blitPipeline) {
        wgpuRenderPipelineRelease(m_blitPipeline);
        m_blitPipeline = nullptr;
    }
    if (m_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
    if (m_sampler) {
        wgpuSamplerRelease(m_sampler);
        m_sampler = nullptr;
    }

    releaseOutput();
    m_initialized = false;
    m_allocatedFrames = 0;
    m_writeIndex = 0;
    m_framesWritten = 0;
}

} // namespace vivid::effects
