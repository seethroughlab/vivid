// Vivid Effects 2D - Bloom Operator Implementation
// Glow effect: threshold -> blur -> combine

#include <vivid/effects/bloom.h>
#include <vivid/effects/gpu_common.h>
#include <string>
#include <vivid/context.h>
#include <cmath>

namespace vivid::effects {

struct BloomUniforms {
    float threshold;
    float intensity;
    float radius;
    float direction;  // 0 = horizontal, 1 = vertical
    float texelW;
    float texelH;
    float _pad[2];
};

Bloom::~Bloom() {
    cleanup();
}

void Bloom::init(Context& ctx) {
    if (!beginInit()) return;
    createOutput(ctx);
    createPipeline(ctx);
}

void Bloom::createPipeline(Context& ctx) {
    // Threshold shader - extracts bright pixels
    const char* fragmentShader = R"(
struct Uniforms {
    threshold: f32,
    intensity: f32,
    radius: f32,
    direction: f32,
    texelW: f32,
    texelH: f32,
    _pad1: f32,
    _pad2: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);
    let brightness = dot(color.rgb, vec3f(0.2126, 0.7152, 0.0722));

    if (brightness > uniforms.threshold) {
        return vec4f(color.rgb * (brightness - uniforms.threshold), color.a);
    }
    return vec4f(0.0, 0.0, 0.0, 0.0);
}
)";
    // Combine shared vertex shader with effect-specific fragment shader
    std::string thresholdShader = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    // Blur shader (same for H and V, just change direction uniform)
    const char* blurFragmentShader = R"(
struct Uniforms {
    threshold: f32,
    intensity: f32,
    radius: f32,
    direction: f32,
    texelW: f32,
    texelH: f32,
    _pad1: f32,
    _pad2: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

fn gaussian(x: f32, sigma: f32) -> f32 {
    return exp(-(x * x) / (2.0 * sigma * sigma));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let texel = vec2f(uniforms.texelW, uniforms.texelH);
    var dir: vec2f;
    if (uniforms.direction < 0.5) {
        dir = vec2f(1.0, 0.0);
    } else {
        dir = vec2f(0.0, 1.0);
    }

    let sigma = uniforms.radius / 3.0;
    let samples = i32(ceil(uniforms.radius));

    var color = vec4f(0.0);
    var totalWeight = 0.0;

    for (var i = -samples; i <= samples; i++) {
        let offset = dir * texel * f32(i);
        let weight = gaussian(f32(i), sigma);
        color += textureSample(inputTex, texSampler, input.uv + offset) * weight;
        totalWeight += weight;
    }

    return color / totalWeight;
}
)";
    // Combine shared vertex shader with effect-specific fragment shader
    std::string blurShader = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + blurFragmentShader;

    // Combine shader - adds bloom to original
    const char* combineFragmentShader = R"(
struct Uniforms {
    threshold: f32,
    intensity: f32,
    radius: f32,
    direction: f32,
    texelW: f32,
    texelH: f32,
    _pad1: f32,
    _pad2: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;
@group(0) @binding(3) var bloomTex: texture_2d<f32>;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let original = textureSample(inputTex, texSampler, input.uv);
    let bloom = textureSample(bloomTex, texSampler, input.uv);
    return vec4f(original.rgb + bloom.rgb * uniforms.intensity, original.a);
}
)";
    // Combine shared vertex shader with effect-specific fragment shader
    std::string combineShader = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + combineFragmentShader;

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(BloomUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());

    // Create intermediate textures
    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = m_width;
    texDesc.size.height = m_height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = EFFECTS_FORMAT;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;

    m_brightTexture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);
    m_blurTexture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = EFFECTS_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    m_brightView = wgpuTextureCreateView(m_brightTexture, &viewDesc);
    m_blurView = wgpuTextureCreateView(m_blurTexture, &viewDesc);

    // Bind group layout for threshold/blur (3 bindings)
    WGPUBindGroupLayoutEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = sizeof(BloomUniforms);

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
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device(), &layoutDesc);

    // Create threshold pipeline
    {
        WGPUShaderSourceWGSL wgslDesc = {};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code = toStringView(thresholdShader.c_str());

        WGPUShaderModuleDescriptor shaderDesc = {};
        shaderDesc.nextInChain = &wgslDesc.chain;
        WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pipelineLayoutDesc);

        WGPUColorTargetState colorTarget = {};
        colorTarget.format = EFFECTS_FORMAT;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragmentState = {};
        fragmentState.module = shaderModule;
        fragmentState.entryPoint = toStringView("fs_main");
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        WGPURenderPipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.vertex.module = shaderModule;
        pipelineDesc.vertex.entryPoint = toStringView("vs_main");
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = ~0u;
        pipelineDesc.fragment = &fragmentState;

        m_thresholdPipeline = wgpuDeviceCreateRenderPipeline(ctx.device(), &pipelineDesc);

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(shaderModule);
    }

    // Create blur pipeline (shared for H and V)
    {
        WGPUShaderSourceWGSL wgslDesc = {};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code = toStringView(blurShader.c_str());

        WGPUShaderModuleDescriptor shaderDesc = {};
        shaderDesc.nextInChain = &wgslDesc.chain;
        WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pipelineLayoutDesc);

        WGPUColorTargetState colorTarget = {};
        colorTarget.format = EFFECTS_FORMAT;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragmentState = {};
        fragmentState.module = shaderModule;
        fragmentState.entryPoint = toStringView("fs_main");
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        WGPURenderPipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.vertex.module = shaderModule;
        pipelineDesc.vertex.entryPoint = toStringView("vs_main");
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = ~0u;
        pipelineDesc.fragment = &fragmentState;

        m_blurHPipeline = wgpuDeviceCreateRenderPipeline(ctx.device(), &pipelineDesc);
        m_blurVPipeline = m_blurHPipeline;
        wgpuRenderPipelineAddRef(m_blurVPipeline);

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(shaderModule);
    }

    // Create combine pipeline (4 bindings - adds bloom texture)
    {
        WGPUBindGroupLayoutEntry combineEntries[4] = {};
        combineEntries[0].binding = 0;
        combineEntries[0].visibility = WGPUShaderStage_Fragment;
        combineEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        combineEntries[0].buffer.minBindingSize = sizeof(BloomUniforms);

        combineEntries[1].binding = 1;
        combineEntries[1].visibility = WGPUShaderStage_Fragment;
        combineEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
        combineEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

        combineEntries[2].binding = 2;
        combineEntries[2].visibility = WGPUShaderStage_Fragment;
        combineEntries[2].sampler.type = WGPUSamplerBindingType_Filtering;

        combineEntries[3].binding = 3;
        combineEntries[3].visibility = WGPUShaderStage_Fragment;
        combineEntries[3].texture.sampleType = WGPUTextureSampleType_Float;
        combineEntries[3].texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor combineLayoutDesc = {};
        combineLayoutDesc.entryCount = 4;
        combineLayoutDesc.entries = combineEntries;
        WGPUBindGroupLayout combineLayout = wgpuDeviceCreateBindGroupLayout(ctx.device(), &combineLayoutDesc);

        WGPUShaderSourceWGSL wgslDesc = {};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code = toStringView(combineShader.c_str());

        WGPUShaderModuleDescriptor shaderDesc = {};
        shaderDesc.nextInChain = &wgslDesc.chain;
        WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &combineLayout;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pipelineLayoutDesc);

        WGPUColorTargetState colorTarget = {};
        colorTarget.format = EFFECTS_FORMAT;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragmentState = {};
        fragmentState.module = shaderModule;
        fragmentState.entryPoint = toStringView("fs_main");
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        WGPURenderPipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.vertex.module = shaderModule;
        pipelineDesc.vertex.entryPoint = toStringView("vs_main");
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = ~0u;
        pipelineDesc.fragment = &fragmentState;

        m_combinePipeline = wgpuDeviceCreateRenderPipeline(ctx.device(), &pipelineDesc);

        wgpuBindGroupLayoutRelease(combineLayout);
        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(shaderModule);
    }
}

void Bloom::process(Context& ctx) {
    if (!isInitialized()) init(ctx);

    // Match input resolution
    matchInputResolution(0);

    WGPUTextureView inView = inputView(0);
    if (!inView) return;

    if (!needsCook()) return;

    float texelW = 1.0f / m_width;
    float texelH = 1.0f / m_height;

    // Pass 1: Threshold - extract bright pixels
    {
        BloomUniforms uniforms = {};
        uniforms.threshold = threshold;
        uniforms.intensity = intensity;
        uniforms.radius = radius;
        uniforms.texelW = texelW;
        uniforms.texelH = texelH;
        wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

        WGPUBindGroupEntry bindEntries[3] = {};
        bindEntries[0].binding = 0;
        bindEntries[0].buffer = m_uniformBuffer;
        bindEntries[0].size = sizeof(BloomUniforms);
        bindEntries[1].binding = 1;
        bindEntries[1].textureView = inView;
        bindEntries[2].binding = 2;
        bindEntries[2].sampler = m_sampler;

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = m_bindGroupLayout;
        bindDesc.entryCount = 3;
        bindDesc.entries = bindEntries;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

        WGPUCommandEncoderDescriptor encoderDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

        WGPURenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = m_brightView;
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {0, 0, 0, 0};

        WGPURenderPassDescriptor passDesc = {};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, m_thresholdPipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(bindGroup);
    }

    // Pass 2 & 3: Blur passes (ping-pong between bright and blur textures)
    for (int i = 0; i < passes; ++i) {
        // Horizontal blur: bright -> blur
        {
            BloomUniforms uniforms = {};
            uniforms.threshold = threshold;
            uniforms.intensity = intensity;
            uniforms.radius = radius;
            uniforms.direction = 0.0f;  // Horizontal
            uniforms.texelW = texelW;
            uniforms.texelH = texelH;
            wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

            WGPUBindGroupEntry bindEntries[3] = {};
            bindEntries[0].binding = 0;
            bindEntries[0].buffer = m_uniformBuffer;
            bindEntries[0].size = sizeof(BloomUniforms);
            bindEntries[1].binding = 1;
            bindEntries[1].textureView = m_brightView;
            bindEntries[2].binding = 2;
            bindEntries[2].sampler = m_sampler;

            WGPUBindGroupDescriptor bindDesc = {};
            bindDesc.layout = m_bindGroupLayout;
            bindDesc.entryCount = 3;
            bindDesc.entries = bindEntries;
            WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

            WGPUCommandEncoderDescriptor encoderDesc = {};
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

            WGPURenderPassColorAttachment colorAttachment = {};
            colorAttachment.view = m_blurView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0, 0, 0, 0};

            WGPURenderPassDescriptor passDesc = {};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(pass, m_blurHPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);

            WGPUCommandBufferDescriptor cmdDesc = {};
            WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
            wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
            wgpuCommandBufferRelease(cmdBuffer);
            wgpuCommandEncoderRelease(encoder);
            wgpuBindGroupRelease(bindGroup);
        }

        // Vertical blur: blur -> bright
        {
            BloomUniforms uniforms = {};
            uniforms.threshold = threshold;
            uniforms.intensity = intensity;
            uniforms.radius = radius;
            uniforms.direction = 1.0f;  // Vertical
            uniforms.texelW = texelW;
            uniforms.texelH = texelH;
            wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

            WGPUBindGroupEntry bindEntries[3] = {};
            bindEntries[0].binding = 0;
            bindEntries[0].buffer = m_uniformBuffer;
            bindEntries[0].size = sizeof(BloomUniforms);
            bindEntries[1].binding = 1;
            bindEntries[1].textureView = m_blurView;
            bindEntries[2].binding = 2;
            bindEntries[2].sampler = m_sampler;

            WGPUBindGroupDescriptor bindDesc = {};
            bindDesc.layout = m_bindGroupLayout;
            bindDesc.entryCount = 3;
            bindDesc.entries = bindEntries;
            WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

            WGPUCommandEncoderDescriptor encoderDesc = {};
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

            WGPURenderPassColorAttachment colorAttachment = {};
            colorAttachment.view = m_brightView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0, 0, 0, 0};

            WGPURenderPassDescriptor passDesc = {};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(pass, m_blurVPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);

            WGPUCommandBufferDescriptor cmdDesc = {};
            WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
            wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
            wgpuCommandBufferRelease(cmdBuffer);
            wgpuCommandEncoderRelease(encoder);
            wgpuBindGroupRelease(bindGroup);
        }
    }

    // Pass 4: Combine - add bloom to original
    {
        BloomUniforms uniforms = {};
        uniforms.threshold = threshold;
        uniforms.intensity = intensity;
        uniforms.radius = radius;
        uniforms.texelW = texelW;
        uniforms.texelH = texelH;
        wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

        // Need to get combine layout from pipeline
        WGPUBindGroupLayout combineLayout = wgpuRenderPipelineGetBindGroupLayout(m_combinePipeline, 0);

        WGPUBindGroupEntry bindEntries[4] = {};
        bindEntries[0].binding = 0;
        bindEntries[0].buffer = m_uniformBuffer;
        bindEntries[0].size = sizeof(BloomUniforms);
        bindEntries[1].binding = 1;
        bindEntries[1].textureView = inView;  // Original
        bindEntries[2].binding = 2;
        bindEntries[2].sampler = m_sampler;
        bindEntries[3].binding = 3;
        bindEntries[3].textureView = m_brightView;  // Blurred bloom

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = combineLayout;
        bindDesc.entryCount = 4;
        bindDesc.entries = bindEntries;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

        WGPUCommandEncoderDescriptor encoderDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

        WGPURenderPassEncoder pass;
        beginRenderPass(pass, encoder);
        wgpuRenderPassEncoderSetPipeline(pass, m_combinePipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        endRenderPass(pass, encoder, ctx);

        wgpuBindGroupLayoutRelease(combineLayout);
        wgpuBindGroupRelease(bindGroup);
    }
    didCook();
}

void Bloom::cleanup() {
    gpu::release(m_thresholdPipeline);
    gpu::release(m_blurHPipeline);
    if (m_blurVPipeline && m_blurVPipeline != m_blurHPipeline) { wgpuRenderPipelineRelease(m_blurVPipeline); }
    m_blurVPipeline = nullptr;
    gpu::release(m_combinePipeline);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    m_sampler = nullptr;
    gpu::release(m_brightView);
    gpu::release(m_brightTexture);
    gpu::release(m_blurView);
    gpu::release(m_blurTexture);
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
