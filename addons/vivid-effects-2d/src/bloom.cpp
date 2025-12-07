// Vivid Effects 2D - Bloom Operator Implementation
// Glow effect: threshold -> blur -> combine

#include <vivid/effects/bloom.h>
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
    if (m_initialized) return;
    createOutput(ctx);
    createPipeline(ctx);
    m_initialized = true;
}

void Bloom::createPipeline(Context& ctx) {
    // Threshold shader - extracts bright pixels
    const char* thresholdShader = R"(
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

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

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
    let color = textureSample(inputTex, texSampler, input.uv);
    let brightness = dot(color.rgb, vec3f(0.2126, 0.7152, 0.0722));

    if (brightness > uniforms.threshold) {
        return vec4f(color.rgb * (brightness - uniforms.threshold), color.a);
    }
    return vec4f(0.0, 0.0, 0.0, 0.0);
}
)";

    // Blur shader (same for H and V, just change direction uniform)
    const char* blurShader = R"(
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

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

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

    // Combine shader - adds bloom to original
    const char* combineShader = R"(
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

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

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
    let original = textureSample(inputTex, texSampler, input.uv);
    let bloom = textureSample(bloomTex, texSampler, input.uv);
    return vec4f(original.rgb + bloom.rgb * uniforms.intensity, original.a);
}
)";

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(BloomUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(ctx.device(), &samplerDesc);

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
        wgslDesc.code = toStringView(thresholdShader);

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
        wgslDesc.code = toStringView(blurShader);

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
        wgslDesc.code = toStringView(combineShader);

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
    if (!m_initialized) init(ctx);

    WGPUTextureView inView = inputView(0);
    if (!inView) return;

    float texelW = 1.0f / m_width;
    float texelH = 1.0f / m_height;

    // Pass 1: Threshold - extract bright pixels
    {
        BloomUniforms uniforms = {};
        uniforms.threshold = m_threshold;
        uniforms.intensity = m_intensity;
        uniforms.radius = m_radius;
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
    for (int i = 0; i < m_passes; ++i) {
        // Horizontal blur: bright -> blur
        {
            BloomUniforms uniforms = {};
            uniforms.threshold = m_threshold;
            uniforms.intensity = m_intensity;
            uniforms.radius = m_radius;
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
            uniforms.threshold = m_threshold;
            uniforms.intensity = m_intensity;
            uniforms.radius = m_radius;
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
        uniforms.threshold = m_threshold;
        uniforms.intensity = m_intensity;
        uniforms.radius = m_radius;
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
}

void Bloom::cleanup() {
    if (m_thresholdPipeline) { wgpuRenderPipelineRelease(m_thresholdPipeline); m_thresholdPipeline = nullptr; }
    if (m_blurHPipeline) { wgpuRenderPipelineRelease(m_blurHPipeline); m_blurHPipeline = nullptr; }
    if (m_blurVPipeline && m_blurVPipeline != m_blurHPipeline) { wgpuRenderPipelineRelease(m_blurVPipeline); }
    m_blurVPipeline = nullptr;
    if (m_combinePipeline) { wgpuRenderPipelineRelease(m_combinePipeline); m_combinePipeline = nullptr; }
    if (m_bindGroupLayout) { wgpuBindGroupLayoutRelease(m_bindGroupLayout); m_bindGroupLayout = nullptr; }
    if (m_uniformBuffer) { wgpuBufferRelease(m_uniformBuffer); m_uniformBuffer = nullptr; }
    if (m_sampler) { wgpuSamplerRelease(m_sampler); m_sampler = nullptr; }
    if (m_brightView) { wgpuTextureViewRelease(m_brightView); m_brightView = nullptr; }
    if (m_brightTexture) { wgpuTextureRelease(m_brightTexture); m_brightTexture = nullptr; }
    if (m_blurView) { wgpuTextureViewRelease(m_blurView); m_blurView = nullptr; }
    if (m_blurTexture) { wgpuTextureRelease(m_blurTexture); m_blurTexture = nullptr; }
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
