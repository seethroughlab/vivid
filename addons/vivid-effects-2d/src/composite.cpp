// Vivid Effects 2D - Composite Operator Implementation

#include <vivid/effects/composite.h>
#include <vivid/context.h>

namespace vivid::effects {

struct CompositeUniforms {
    int mode;
    float opacity;
    int inputCount;
    float _padding;
};

Composite::~Composite() {
    cleanup();
}

void Composite::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createDummyTexture(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void Composite::createDummyTexture(Context& ctx) {
    // Create a 1x1 transparent texture for unused input slots
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("Composite Dummy Texture");
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {1, 1, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.format = EFFECTS_FORMAT;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    m_dummyTexture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    // Initialize to transparent black (RGBA16Float = 8 bytes per pixel)
    uint16_t pixel[4] = {0, 0, 0, 0};  // 4 x 16-bit floats (all zeros = transparent black)
    WGPUTexelCopyTextureInfo dest = {};
    dest.texture = m_dummyTexture;
    dest.mipLevel = 0;
    dest.origin = {0, 0, 0};
    dest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = 8;  // RGBA16Float = 8 bytes per pixel
    layout.rowsPerImage = 1;

    WGPUExtent3D extent = {1, 1, 1};
    wgpuQueueWriteTexture(ctx.queue(), &dest, pixel, sizeof(pixel), &layout, &extent);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = EFFECTS_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    m_dummyView = wgpuTextureCreateView(m_dummyTexture, &viewDesc);
}

void Composite::createPipeline(Context& ctx) {
    // Shader with 8 texture inputs
    const char* shaderSource = R"(
struct Uniforms {
    mode: i32,
    opacity: f32,
    inputCount: i32,
    _padding: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var tex0: texture_2d<f32>;
@group(0) @binding(3) var tex1: texture_2d<f32>;
@group(0) @binding(4) var tex2: texture_2d<f32>;
@group(0) @binding(5) var tex3: texture_2d<f32>;
@group(0) @binding(6) var tex4: texture_2d<f32>;
@group(0) @binding(7) var tex5: texture_2d<f32>;
@group(0) @binding(8) var tex6: texture_2d<f32>;
@group(0) @binding(9) var tex7: texture_2d<f32>;

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

fn blendOver(base: vec4f, blend: vec4f, opacity: f32) -> vec4f {
    let a = blend.a * opacity;
    return vec4f(mix(base.rgb, blend.rgb, a), max(base.a, a));
}

fn blendAdd(base: vec4f, blend: vec4f, opacity: f32) -> vec4f {
    return vec4f(base.rgb + blend.rgb * blend.a * opacity, max(base.a, blend.a * opacity));
}

fn blendMultiply(base: vec4f, blend: vec4f, opacity: f32) -> vec4f {
    let result = base.rgb * blend.rgb;
    return vec4f(mix(base.rgb, result, blend.a * opacity), base.a);
}

fn blendScreen(base: vec4f, blend: vec4f, opacity: f32) -> vec4f {
    let result = 1.0 - (1.0 - base.rgb) * (1.0 - blend.rgb);
    return vec4f(mix(base.rgb, result, blend.a * opacity), max(base.a, blend.a * opacity));
}

fn blendOverlay(base: vec4f, blend: vec4f, opacity: f32) -> vec4f {
    var result: vec3f;
    for (var i = 0; i < 3; i++) {
        if (base[i] < 0.5) {
            result[i] = 2.0 * base[i] * blend[i];
        } else {
            result[i] = 1.0 - 2.0 * (1.0 - base[i]) * (1.0 - blend[i]);
        }
    }
    return vec4f(mix(base.rgb, result, blend.a * opacity), max(base.a, blend.a * opacity));
}

fn blendDifference(base: vec4f, blend: vec4f, opacity: f32) -> vec4f {
    let result = abs(base.rgb - blend.rgb);
    return vec4f(mix(base.rgb, result, blend.a * opacity), max(base.a, blend.a * opacity));
}

fn blend(base: vec4f, layer: vec4f, mode: i32, opacity: f32) -> vec4f {
    switch(mode) {
        case 0: { return blendOver(base, layer, opacity); }
        case 1: { return blendAdd(base, layer, opacity); }
        case 2: { return blendMultiply(base, layer, opacity); }
        case 3: { return blendScreen(base, layer, opacity); }
        case 4: { return blendOverlay(base, layer, opacity); }
        case 5: { return blendDifference(base, layer, opacity); }
        default: { return blendOver(base, layer, opacity); }
    }
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Start with first input as base
    var result = textureSample(tex0, texSampler, input.uv);

    // Blend remaining inputs sequentially
    if (uniforms.inputCount > 1) {
        let s1 = textureSample(tex1, texSampler, input.uv);
        result = blend(result, s1, uniforms.mode, uniforms.opacity);
    }
    if (uniforms.inputCount > 2) {
        let s2 = textureSample(tex2, texSampler, input.uv);
        result = blend(result, s2, uniforms.mode, uniforms.opacity);
    }
    if (uniforms.inputCount > 3) {
        let s3 = textureSample(tex3, texSampler, input.uv);
        result = blend(result, s3, uniforms.mode, uniforms.opacity);
    }
    if (uniforms.inputCount > 4) {
        let s4 = textureSample(tex4, texSampler, input.uv);
        result = blend(result, s4, uniforms.mode, uniforms.opacity);
    }
    if (uniforms.inputCount > 5) {
        let s5 = textureSample(tex5, texSampler, input.uv);
        result = blend(result, s5, uniforms.mode, uniforms.opacity);
    }
    if (uniforms.inputCount > 6) {
        let s6 = textureSample(tex6, texSampler, input.uv);
        result = blend(result, s6, uniforms.mode, uniforms.opacity);
    }
    if (uniforms.inputCount > 7) {
        let s7 = textureSample(tex7, texSampler, input.uv);
        result = blend(result, s7, uniforms.mode, uniforms.opacity);
    }

    return result;
}
)";

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("Composite Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Composite Uniforms");
    bufferDesc.size = sizeof(CompositeUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(ctx.device(), &samplerDesc);

    // Create bind group layout with 8 texture slots
    // 0: uniforms, 1: sampler, 2-9: textures
    WGPUBindGroupLayoutEntry layoutEntries[10] = {};

    // Uniform buffer
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].buffer.minBindingSize = sizeof(CompositeUniforms);

    // Sampler
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    // 8 textures
    for (int i = 0; i < COMPOSITE_MAX_INPUTS; ++i) {
        layoutEntries[2 + i].binding = 2 + i;
        layoutEntries[2 + i].visibility = WGPUShaderStage_Fragment;
        layoutEntries[2 + i].texture.sampleType = WGPUTextureSampleType_Float;
        layoutEntries[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.label = toStringView("Composite Bind Group Layout");
    layoutDesc.entryCount = 10;
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
    pipelineDesc.label = toStringView("Composite Pipeline");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device(), &pipelineDesc);

    // Cleanup
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void Composite::updateBindGroup(Context& ctx) {
    // Gather current input views
    std::array<WGPUTextureView, COMPOSITE_MAX_INPUTS> currentViews = {};
    int activeCount = 0;

    for (int i = 0; i < COMPOSITE_MAX_INPUTS; ++i) {
        WGPUTextureView view = inputView(i);
        if (view) {
            currentViews[i] = view;
            activeCount = i + 1;
        } else {
            currentViews[i] = m_dummyView;
        }
    }

    // Check if we need to recreate the bind group
    bool needsUpdate = (m_bindGroup == nullptr) || (activeCount != m_lastInputCount);
    if (!needsUpdate) {
        for (int i = 0; i < COMPOSITE_MAX_INPUTS; ++i) {
            if (currentViews[i] != m_lastInputViews[i]) {
                needsUpdate = true;
                break;
            }
        }
    }

    if (!needsUpdate) {
        return;
    }

    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
        m_bindGroup = nullptr;
    }

    // Need at least one input
    if (activeCount == 0) {
        return;
    }

    WGPUBindGroupEntry entries[10] = {};

    // Uniform buffer
    entries[0].binding = 0;
    entries[0].buffer = m_uniformBuffer;
    entries[0].offset = 0;
    entries[0].size = sizeof(CompositeUniforms);

    // Sampler
    entries[1].binding = 1;
    entries[1].sampler = m_sampler;

    // Textures
    for (int i = 0; i < COMPOSITE_MAX_INPUTS; ++i) {
        entries[2 + i].binding = 2 + i;
        entries[2 + i].textureView = currentViews[i];
    }

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.label = toStringView("Composite Bind Group");
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 10;
    bindDesc.entries = entries;
    m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    m_lastInputViews = currentViews;
    m_lastInputCount = activeCount;
}

void Composite::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    // Match input resolution (from first input)
    matchInputResolution(0);

    updateBindGroup(ctx);

    if (!m_bindGroup) {
        return; // Missing inputs
    }

    // Skip if nothing changed
    if (!needsCook()) return;

    // Count active inputs
    int activeCount = 0;
    for (int i = 0; i < COMPOSITE_MAX_INPUTS; ++i) {
        if (inputView(i)) {
            activeCount = i + 1;
        }
    }

    // Update uniforms
    CompositeUniforms uniforms = {};
    uniforms.mode = static_cast<int>(m_mode);
    uniforms.opacity = static_cast<float>(m_opacity);
    uniforms.inputCount = activeCount;
    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

    // Begin render pass
    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);

    // Draw
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    // End render pass
    endRenderPass(pass, encoder, ctx);

    didCook();
}

void Composite::cleanup() {
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
        m_bindGroup = nullptr;
    }
    if (m_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
    if (m_uniformBuffer) {
        wgpuBufferRelease(m_uniformBuffer);
        m_uniformBuffer = nullptr;
    }
    if (m_sampler) {
        wgpuSamplerRelease(m_sampler);
        m_sampler = nullptr;
    }
    if (m_dummyView) {
        wgpuTextureViewRelease(m_dummyView);
        m_dummyView = nullptr;
    }
    if (m_dummyTexture) {
        wgpuTextureRelease(m_dummyTexture);
        m_dummyTexture = nullptr;
    }
    releaseOutput();
    m_initialized = false;
    m_lastInputViews = {};
    m_lastInputCount = 0;
}

} // namespace vivid::effects
