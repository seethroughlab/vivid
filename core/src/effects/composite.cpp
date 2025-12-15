// Vivid Effects 2D - Composite Operator Implementation

#include <vivid/effects/composite.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <string>
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
    const char* fragmentShader = R"(
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

    // Combine shared vertex shader with effect-specific fragment shader
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    // Use PipelineBuilder
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(CompositeUniforms))
           .sampler(1)
           .texture(2)    // tex0
           .texture(3)    // tex1
           .texture(4)    // tex2
           .texture(5)    // tex3
           .texture(6)    // tex4
           .texture(7)    // tex5
           .texture(8)    // tex6
           .texture(9);   // tex7

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Composite Uniforms");
    bufferDesc.size = sizeof(CompositeUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());
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
    uniforms.opacity = static_cast<float>(opacity);
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
    gpu::release(m_pipeline);
    gpu::release(m_bindGroup);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    m_sampler = nullptr;
    gpu::release(m_dummyView);
    gpu::release(m_dummyTexture);
    releaseOutput();
    m_initialized = false;
    m_lastInputViews = {};
    m_lastInputCount = 0;
}

} // namespace vivid::effects
