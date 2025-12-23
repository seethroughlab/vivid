#include <vivid/overlay_canvas.h>
#include <vivid/context.h>
#include "effects/font_atlas.h"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace vivid {

// Shader for overlay rendering (no stencil, simple alpha blending)
static const char* OVERLAY_SHADER = R"(
struct Uniforms {
    resolution: vec2f,
    padding: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec2f,
    @location(1) uv: vec2f,
    @location(2) color: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    // Convert pixel coords to clip space (-1 to 1)
    let clipX = (in.position.x / uniforms.resolution.x) * 2.0 - 1.0;
    let clipY = 1.0 - (in.position.y / uniforms.resolution.y) * 2.0;
    out.position = vec4f(clipX, clipY, 0.0, 1.0);
    out.uv = in.uv;
    out.color = in.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let texColor = textureSample(tex, texSampler, in.uv);
    // For text: texture has alpha in .a channel
    // For solids: texture is white (1,1,1,1)
    return vec4f(in.color.rgb * texColor.rgb, in.color.a * texColor.a);
}
)";

// Helper to convert string to WGPUStringView
static WGPUStringView toStringView(const char* str) {
    return {str, strlen(str)};
}

OverlayCanvas::OverlayCanvas() = default;

OverlayCanvas::~OverlayCanvas() {
    cleanup();
}

void OverlayCanvas::cleanup() {
    // Release font bind groups
    for (int i = 0; i < 3; i++) {
        if (m_fontBindGroups[i]) {
            wgpuBindGroupRelease(m_fontBindGroups[i]);
            m_fontBindGroups[i] = nullptr;
        }
        m_fonts[i].reset();
    }

    // Release white texture resources
    if (m_whiteBindGroup) {
        wgpuBindGroupRelease(m_whiteBindGroup);
        m_whiteBindGroup = nullptr;
    }
    if (m_whiteTextureView) {
        wgpuTextureViewRelease(m_whiteTextureView);
        m_whiteTextureView = nullptr;
    }
    if (m_whiteTexture) {
        wgpuTextureRelease(m_whiteTexture);
        m_whiteTexture = nullptr;
    }

    // Release buffers
    if (m_solidVertexBuffer) {
        wgpuBufferRelease(m_solidVertexBuffer);
        m_solidVertexBuffer = nullptr;
    }
    if (m_solidIndexBuffer) {
        wgpuBufferRelease(m_solidIndexBuffer);
        m_solidIndexBuffer = nullptr;
    }
    for (int i = 0; i < 3; i++) {
        if (m_textVertexBuffer[i]) {
            wgpuBufferRelease(m_textVertexBuffer[i]);
            m_textVertexBuffer[i] = nullptr;
        }
        if (m_textIndexBuffer[i]) {
            wgpuBufferRelease(m_textIndexBuffer[i]);
            m_textIndexBuffer[i] = nullptr;
        }
        m_textVertexCapacity[i] = 0;
        m_textIndexCapacity[i] = 0;
    }
    m_solidVertexCapacity = 0;
    m_solidIndexCapacity = 0;

    // Release pipeline resources
    if (m_sampler) {
        wgpuSamplerRelease(m_sampler);
        m_sampler = nullptr;
    }
    if (m_uniformBuffer) {
        wgpuBufferRelease(m_uniformBuffer);
        m_uniformBuffer = nullptr;
    }
    if (m_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }

    m_initialized = false;
}

bool OverlayCanvas::init(Context& ctx, WGPUTextureFormat surfaceFormat) {
    if (m_initialized) return true;

    m_device = ctx.device();
    m_queue = ctx.queue();
    m_surfaceFormat = surfaceFormat;

    createPipeline(ctx);
    createWhiteTexture(ctx);

    m_initialized = true;
    return true;
}

void OverlayCanvas::createPipeline(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(OVERLAY_SHADER);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Bind group layout
    WGPUBindGroupLayoutEntry entries[3] = {};

    // Uniforms
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = 16;

    // Sampler
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    // Texture
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 3;
    layoutDesc.entries = entries;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Vertex attributes
    WGPUVertexAttribute attrs[3] = {};
    attrs[0].format = WGPUVertexFormat_Float32x2;  // position
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x2;  // uv
    attrs[1].offset = 8;
    attrs[1].shaderLocation = 1;
    attrs[2].format = WGPUVertexFormat_Float32x4;  // color
    attrs[2].offset = 16;
    attrs[2].shaderLocation = 2;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(OverlayVertex);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 3;
    vertexLayout.attributes = attrs;

    // Color target with alpha blending
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = m_surfaceFormat;  // Use actual surface format
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Render pipeline - NO stencil/depth (simpler, works with any render pass)
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.depthStencil = nullptr;  // No stencil!
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = 16;
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(device, &samplerDesc);
}

void OverlayCanvas::createWhiteTexture(Context& ctx) {
    WGPUDevice device = ctx.device();
    WGPUQueue queue = ctx.queue();

    // Create 1x1 white texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {1, 1, 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    m_whiteTexture = wgpuDeviceCreateTexture(device, &texDesc);

    uint8_t white[4] = {255, 255, 255, 255};
    WGPUTexelCopyTextureInfo dest = {};
    dest.texture = m_whiteTexture;
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
    m_whiteTextureView = wgpuTextureCreateView(m_whiteTexture, &viewDesc);

    // Create bind group for white texture
    WGPUBindGroupEntry bgEntries[3] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].buffer = m_uniformBuffer;
    bgEntries[0].size = 16;
    bgEntries[1].binding = 1;
    bgEntries[1].sampler = m_sampler;
    bgEntries[2].binding = 2;
    bgEntries[2].textureView = m_whiteTextureView;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = m_bindGroupLayout;
    bgDesc.entryCount = 3;
    bgDesc.entries = bgEntries;
    m_whiteBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
}

bool OverlayCanvas::loadFont(Context& ctx, const std::string& path, float fontSize) {
    return loadFontSize(ctx, path, fontSize, 0);
}

bool OverlayCanvas::loadFontSize(Context& ctx, const std::string& path, float fontSize, int index) {
    if (index < 0 || index >= 3) return false;

    m_fonts[index] = std::make_unique<FontAtlas>();
    if (!m_fonts[index]->load(ctx, path, fontSize)) {
        m_fonts[index].reset();
        return false;
    }

    // Create bind group for this font
    if (m_fontBindGroups[index]) {
        wgpuBindGroupRelease(m_fontBindGroups[index]);
    }

    WGPUBindGroupEntry bgEntries[3] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].buffer = m_uniformBuffer;
    bgEntries[0].size = 16;
    bgEntries[1].binding = 1;
    bgEntries[1].sampler = m_sampler;
    bgEntries[2].binding = 2;
    bgEntries[2].textureView = m_fonts[index]->textureView();

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = m_bindGroupLayout;
    bgDesc.entryCount = 3;
    bgDesc.entries = bgEntries;
    m_fontBindGroups[index] = wgpuDeviceCreateBindGroup(ctx.device(), &bgDesc);

    return true;
}

void OverlayCanvas::begin(int width, int height) {
    m_width = width;
    m_height = height;

    // Clear batched geometry
    m_solidVertices.clear();
    m_solidIndices.clear();
    // Clear all font batches
    for (int i = 0; i < 3; i++) {
        m_textVertices[i].clear();
        m_textIndices[i].clear();
    }
    m_texturedRects.clear();
    // Clear topmost layer
    m_topmostVertices.clear();
    m_topmostIndices.clear();
    for (int i = 0; i < 3; i++) {
        m_topmostTextVertices[i].clear();
        m_topmostTextIndices[i].clear();
    }

    // Reset transform
    m_transform = glm::mat3(1.0f);
    m_transformStack.clear();
}

void OverlayCanvas::render(WGPURenderPassEncoder pass) {
    if (!m_initialized) return;
    // Check if any text batches have content
    bool hasText = false;
    for (int i = 0; i < 3; i++) {
        if (!m_textVertices[i].empty()) {
            hasText = true;
            break;
        }
    }
    if (m_solidVertices.empty() && !hasText && m_texturedRects.empty()) return;

    // Update uniforms
    float uniforms[4] = {static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 0.0f};
    wgpuQueueWriteBuffer(m_queue, m_uniformBuffer, 0, uniforms, sizeof(uniforms));

    // Set pipeline
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);

    // Render solid primitives first (node backgrounds, etc.)
    if (!m_solidVertices.empty()) {
        // Ensure buffer capacity
        size_t neededVertexSize = m_solidVertices.size() * sizeof(OverlayVertex);
        size_t neededIndexSize = m_solidIndices.size() * sizeof(uint32_t);

        if (neededVertexSize > m_solidVertexCapacity) {
            if (m_solidVertexBuffer) wgpuBufferRelease(m_solidVertexBuffer);
            size_t newCapacity = std::max(neededVertexSize, INITIAL_VERTEX_CAPACITY * sizeof(OverlayVertex));
            newCapacity = std::max(newCapacity, m_solidVertexCapacity * 2);
            WGPUBufferDescriptor vbDesc = {};
            vbDesc.size = newCapacity;
            vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            m_solidVertexBuffer = wgpuDeviceCreateBuffer(m_device, &vbDesc);
            m_solidVertexCapacity = newCapacity;
        }
        if (neededIndexSize > m_solidIndexCapacity) {
            if (m_solidIndexBuffer) wgpuBufferRelease(m_solidIndexBuffer);
            size_t newCapacity = std::max(neededIndexSize, INITIAL_INDEX_CAPACITY * sizeof(uint32_t));
            newCapacity = std::max(newCapacity, m_solidIndexCapacity * 2);
            WGPUBufferDescriptor ibDesc = {};
            ibDesc.size = newCapacity;
            ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            m_solidIndexBuffer = wgpuDeviceCreateBuffer(m_device, &ibDesc);
            m_solidIndexCapacity = newCapacity;
        }

        wgpuQueueWriteBuffer(m_queue, m_solidVertexBuffer, 0, m_solidVertices.data(), neededVertexSize);
        wgpuQueueWriteBuffer(m_queue, m_solidIndexBuffer, 0, m_solidIndices.data(), neededIndexSize);

        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_whiteBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_solidVertexBuffer, 0, neededVertexSize);
        wgpuRenderPassEncoderSetIndexBuffer(pass, m_solidIndexBuffer, WGPUIndexFormat_Uint32, 0, neededIndexSize);
        wgpuRenderPassEncoderDrawIndexed(pass, static_cast<uint32_t>(m_solidIndices.size()), 1, 0, 0, 0);
    }

    // Render textured rects (operator previews)
    if (!m_texturedRects.empty()) {
        // Create temporary vertex/index buffers for textured quads
        // Each rect is 4 vertices (quad) and 6 indices (2 triangles)
        std::vector<OverlayVertex> texVertices;
        std::vector<uint32_t> texIndices;
        texVertices.reserve(m_texturedRects.size() * 4);
        texIndices.reserve(m_texturedRects.size() * 6);

        // Temporary buffers and bind groups (released after render)
        std::vector<WGPUBindGroup> tempBindGroups;

        for (const auto& rect : m_texturedRects) {
            // Build vertices for this quad
            uint32_t baseIdx = static_cast<uint32_t>(texVertices.size());
            glm::vec2 p0 = rect.pos;
            glm::vec2 p1 = {rect.pos.x + rect.size.x, rect.pos.y};
            glm::vec2 p2 = rect.pos + rect.size;
            glm::vec2 p3 = {rect.pos.x, rect.pos.y + rect.size.y};

            texVertices.push_back({p0, {0, 0}, rect.tint});
            texVertices.push_back({p1, {1, 0}, rect.tint});
            texVertices.push_back({p2, {1, 1}, rect.tint});
            texVertices.push_back({p3, {0, 1}, rect.tint});

            texIndices.push_back(baseIdx + 0);
            texIndices.push_back(baseIdx + 1);
            texIndices.push_back(baseIdx + 2);
            texIndices.push_back(baseIdx + 0);
            texIndices.push_back(baseIdx + 2);
            texIndices.push_back(baseIdx + 3);

            // Create bind group for this texture
            WGPUBindGroupEntry bgEntries[3] = {};
            bgEntries[0].binding = 0;
            bgEntries[0].buffer = m_uniformBuffer;
            bgEntries[0].size = 16;
            bgEntries[1].binding = 1;
            bgEntries[1].sampler = m_sampler;
            bgEntries[2].binding = 2;
            bgEntries[2].textureView = rect.textureView;

            WGPUBindGroupDescriptor bgDesc = {};
            bgDesc.layout = m_bindGroupLayout;
            bgDesc.entryCount = 3;
            bgDesc.entries = bgEntries;
            WGPUBindGroup bg = wgpuDeviceCreateBindGroup(m_device, &bgDesc);
            tempBindGroups.push_back(bg);
        }

        if (!texVertices.empty()) {
            // Create temporary buffers
            size_t vertexSize = texVertices.size() * sizeof(OverlayVertex);
            size_t indexSize = texIndices.size() * sizeof(uint32_t);

            WGPUBufferDescriptor vbDesc = {};
            vbDesc.size = vertexSize;
            vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(m_device, &vbDesc);

            WGPUBufferDescriptor ibDesc = {};
            ibDesc.size = indexSize;
            ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(m_device, &ibDesc);

            wgpuQueueWriteBuffer(m_queue, vertexBuffer, 0, texVertices.data(), vertexSize);
            wgpuQueueWriteBuffer(m_queue, indexBuffer, 0, texIndices.data(), indexSize);

            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, vertexSize);
            wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32, 0, indexSize);

            // Draw each textured rect with its own bind group
            for (size_t i = 0; i < m_texturedRects.size(); i++) {
                wgpuRenderPassEncoderSetBindGroup(pass, 0, tempBindGroups[i], 0, nullptr);
                wgpuRenderPassEncoderDrawIndexed(pass, 6, 1, static_cast<uint32_t>(i * 6), 0, 0);
            }

            // Release temporary buffers
            wgpuBufferRelease(vertexBuffer);
            wgpuBufferRelease(indexBuffer);
        }

        // Release temporary bind groups
        for (auto bg : tempBindGroups) {
            wgpuBindGroupRelease(bg);
        }
    }

    // Render text for each font batch (each font has its own buffers)
    for (int fontIdx = 0; fontIdx < 3; fontIdx++) {
        if (m_textVertices[fontIdx].empty() || !m_fontBindGroups[fontIdx]) continue;

        size_t neededVertexSize = m_textVertices[fontIdx].size() * sizeof(OverlayVertex);
        size_t neededIndexSize = m_textIndices[fontIdx].size() * sizeof(uint32_t);

        // Ensure per-font buffer capacity
        if (neededVertexSize > m_textVertexCapacity[fontIdx]) {
            if (m_textVertexBuffer[fontIdx]) wgpuBufferRelease(m_textVertexBuffer[fontIdx]);
            size_t newCapacity = std::max(neededVertexSize, INITIAL_VERTEX_CAPACITY * sizeof(OverlayVertex));
            newCapacity = std::max(newCapacity, m_textVertexCapacity[fontIdx] * 2);
            WGPUBufferDescriptor vbDesc = {};
            vbDesc.size = newCapacity;
            vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            m_textVertexBuffer[fontIdx] = wgpuDeviceCreateBuffer(m_device, &vbDesc);
            m_textVertexCapacity[fontIdx] = newCapacity;
        }
        if (neededIndexSize > m_textIndexCapacity[fontIdx]) {
            if (m_textIndexBuffer[fontIdx]) wgpuBufferRelease(m_textIndexBuffer[fontIdx]);
            size_t newCapacity = std::max(neededIndexSize, INITIAL_INDEX_CAPACITY * sizeof(uint32_t));
            newCapacity = std::max(newCapacity, m_textIndexCapacity[fontIdx] * 2);
            WGPUBufferDescriptor ibDesc = {};
            ibDesc.size = newCapacity;
            ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            m_textIndexBuffer[fontIdx] = wgpuDeviceCreateBuffer(m_device, &ibDesc);
            m_textIndexCapacity[fontIdx] = newCapacity;
        }

        wgpuQueueWriteBuffer(m_queue, m_textVertexBuffer[fontIdx], 0, m_textVertices[fontIdx].data(), neededVertexSize);
        wgpuQueueWriteBuffer(m_queue, m_textIndexBuffer[fontIdx], 0, m_textIndices[fontIdx].data(), neededIndexSize);

        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_fontBindGroups[fontIdx], 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_textVertexBuffer[fontIdx], 0, neededVertexSize);
        wgpuRenderPassEncoderSetIndexBuffer(pass, m_textIndexBuffer[fontIdx], WGPUIndexFormat_Uint32, 0, neededIndexSize);
        wgpuRenderPassEncoderDrawIndexed(pass, static_cast<uint32_t>(m_textIndices[fontIdx].size()), 1, 0, 0, 0);
    }

    // -------------------------------------------------------------------------
    // TOPMOST LAYER - rendered last, on top of everything (for tooltips)
    // -------------------------------------------------------------------------

    // Topmost solid primitives (tooltip backgrounds)
    if (!m_topmostVertices.empty()) {
        size_t neededVertexSize = m_topmostVertices.size() * sizeof(OverlayVertex);
        size_t neededIndexSize = m_topmostIndices.size() * sizeof(uint32_t);

        // Reuse solid buffers for topmost (they've already been rendered)
        if (neededVertexSize > m_solidVertexCapacity) {
            if (m_solidVertexBuffer) wgpuBufferRelease(m_solidVertexBuffer);
            size_t newCapacity = std::max(neededVertexSize, INITIAL_VERTEX_CAPACITY * sizeof(OverlayVertex));
            WGPUBufferDescriptor vbDesc = {};
            vbDesc.size = newCapacity;
            vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            m_solidVertexBuffer = wgpuDeviceCreateBuffer(m_device, &vbDesc);
            m_solidVertexCapacity = newCapacity;
        }
        if (neededIndexSize > m_solidIndexCapacity) {
            if (m_solidIndexBuffer) wgpuBufferRelease(m_solidIndexBuffer);
            size_t newCapacity = std::max(neededIndexSize, INITIAL_INDEX_CAPACITY * sizeof(uint32_t));
            WGPUBufferDescriptor ibDesc = {};
            ibDesc.size = newCapacity;
            ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            m_solidIndexBuffer = wgpuDeviceCreateBuffer(m_device, &ibDesc);
            m_solidIndexCapacity = newCapacity;
        }

        wgpuQueueWriteBuffer(m_queue, m_solidVertexBuffer, 0, m_topmostVertices.data(), neededVertexSize);
        wgpuQueueWriteBuffer(m_queue, m_solidIndexBuffer, 0, m_topmostIndices.data(), neededIndexSize);

        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_whiteBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_solidVertexBuffer, 0, neededVertexSize);
        wgpuRenderPassEncoderSetIndexBuffer(pass, m_solidIndexBuffer, WGPUIndexFormat_Uint32, 0, neededIndexSize);
        wgpuRenderPassEncoderDrawIndexed(pass, static_cast<uint32_t>(m_topmostIndices.size()), 1, 0, 0, 0);
    }

    // Topmost text (tooltip text)
    for (int fontIdx = 0; fontIdx < 3; fontIdx++) {
        if (m_topmostTextVertices[fontIdx].empty() || !m_fontBindGroups[fontIdx]) continue;

        size_t neededVertexSize = m_topmostTextVertices[fontIdx].size() * sizeof(OverlayVertex);
        size_t neededIndexSize = m_topmostTextIndices[fontIdx].size() * sizeof(uint32_t);

        // Reuse text buffers (they've already been rendered)
        if (neededVertexSize > m_textVertexCapacity[fontIdx]) {
            if (m_textVertexBuffer[fontIdx]) wgpuBufferRelease(m_textVertexBuffer[fontIdx]);
            size_t newCapacity = std::max(neededVertexSize, INITIAL_VERTEX_CAPACITY * sizeof(OverlayVertex));
            WGPUBufferDescriptor vbDesc = {};
            vbDesc.size = newCapacity;
            vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            m_textVertexBuffer[fontIdx] = wgpuDeviceCreateBuffer(m_device, &vbDesc);
            m_textVertexCapacity[fontIdx] = newCapacity;
        }
        if (neededIndexSize > m_textIndexCapacity[fontIdx]) {
            if (m_textIndexBuffer[fontIdx]) wgpuBufferRelease(m_textIndexBuffer[fontIdx]);
            size_t newCapacity = std::max(neededIndexSize, INITIAL_INDEX_CAPACITY * sizeof(uint32_t));
            WGPUBufferDescriptor ibDesc = {};
            ibDesc.size = newCapacity;
            ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            m_textIndexBuffer[fontIdx] = wgpuDeviceCreateBuffer(m_device, &ibDesc);
            m_textIndexCapacity[fontIdx] = newCapacity;
        }

        wgpuQueueWriteBuffer(m_queue, m_textVertexBuffer[fontIdx], 0, m_topmostTextVertices[fontIdx].data(), neededVertexSize);
        wgpuQueueWriteBuffer(m_queue, m_textIndexBuffer[fontIdx], 0, m_topmostTextIndices[fontIdx].data(), neededIndexSize);

        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_fontBindGroups[fontIdx], 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_textVertexBuffer[fontIdx], 0, neededVertexSize);
        wgpuRenderPassEncoderSetIndexBuffer(pass, m_textIndexBuffer[fontIdx], WGPUIndexFormat_Uint32, 0, neededIndexSize);
        wgpuRenderPassEncoderDrawIndexed(pass, static_cast<uint32_t>(m_topmostTextIndices[fontIdx].size()), 1, 0, 0, 0);
    }
}

// -------------------------------------------------------------------------
// Transform
// -------------------------------------------------------------------------

void OverlayCanvas::save() {
    m_transformStack.push_back(m_transform);
}

void OverlayCanvas::restore() {
    if (!m_transformStack.empty()) {
        m_transform = m_transformStack.back();
        m_transformStack.pop_back();
    }
}

void OverlayCanvas::setTransform(const glm::mat3& matrix) {
    m_transform = matrix;
}

void OverlayCanvas::resetTransform() {
    m_transform = glm::mat3(1.0f);
}

void OverlayCanvas::translate(float x, float y) {
    glm::mat3 t(1.0f);
    t[2][0] = x;
    t[2][1] = y;
    m_transform = m_transform * t;
}

void OverlayCanvas::scale(float s) {
    scale(s, s);
}

void OverlayCanvas::scale(float sx, float sy) {
    glm::mat3 s(1.0f);
    s[0][0] = sx;
    s[1][1] = sy;
    m_transform = m_transform * s;
}

glm::vec2 OverlayCanvas::transformPoint(const glm::vec2& p) const {
    glm::vec3 tp = m_transform * glm::vec3(p, 1.0f);
    return glm::vec2(tp.x, tp.y);
}

glm::vec2 OverlayCanvas::inverseTransformPoint(const glm::vec2& p) const {
    glm::mat3 inv = glm::inverse(m_transform);
    glm::vec3 tp = inv * glm::vec3(p, 1.0f);
    return glm::vec2(tp.x, tp.y);
}

// -------------------------------------------------------------------------
// Primitives
// -------------------------------------------------------------------------

void OverlayCanvas::addQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, const glm::vec4& color) {
    uint32_t baseIndex = static_cast<uint32_t>(m_solidVertices.size());
    glm::vec2 uv(0.5f, 0.5f);

    m_solidVertices.push_back({p0, uv, color});
    m_solidVertices.push_back({p1, uv, color});
    m_solidVertices.push_back({p2, uv, color});
    m_solidVertices.push_back({p3, uv, color});

    m_solidIndices.push_back(baseIndex + 0);
    m_solidIndices.push_back(baseIndex + 1);
    m_solidIndices.push_back(baseIndex + 2);
    m_solidIndices.push_back(baseIndex + 0);
    m_solidIndices.push_back(baseIndex + 2);
    m_solidIndices.push_back(baseIndex + 3);
}

void OverlayCanvas::addTextQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                                 glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3,
                                 const glm::vec4& color, int fontIndex) {
    if (fontIndex < 0 || fontIndex >= 3) fontIndex = 0;
    uint32_t baseIndex = static_cast<uint32_t>(m_textVertices[fontIndex].size());

    m_textVertices[fontIndex].push_back({p0, uv0, color});
    m_textVertices[fontIndex].push_back({p1, uv1, color});
    m_textVertices[fontIndex].push_back({p2, uv2, color});
    m_textVertices[fontIndex].push_back({p3, uv3, color});

    m_textIndices[fontIndex].push_back(baseIndex + 0);
    m_textIndices[fontIndex].push_back(baseIndex + 1);
    m_textIndices[fontIndex].push_back(baseIndex + 2);
    m_textIndices[fontIndex].push_back(baseIndex + 0);
    m_textIndices[fontIndex].push_back(baseIndex + 2);
    m_textIndices[fontIndex].push_back(baseIndex + 3);
}

void OverlayCanvas::fillRect(float x, float y, float w, float h, const glm::vec4& color) {
    glm::vec2 p0 = transformPoint({x, y});
    glm::vec2 p1 = transformPoint({x + w, y});
    glm::vec2 p2 = transformPoint({x + w, y + h});
    glm::vec2 p3 = transformPoint({x, y + h});
    addQuad(p0, p1, p2, p3, color);
}

void OverlayCanvas::texturedRect(float x, float y, float w, float h, WGPUTextureView textureView, const glm::vec4& tint) {
    if (!textureView) return;

    // Transform corners to screen space
    glm::vec2 p0 = transformPoint({x, y});
    glm::vec2 p1 = transformPoint({x + w, y + h});

    // Store for deferred rendering
    TexturedRect rect;
    rect.pos = p0;
    rect.size = p1 - p0;
    rect.textureView = textureView;
    rect.tint = tint;
    m_texturedRects.push_back(rect);
}

void OverlayCanvas::strokeRect(float x, float y, float w, float h, float lineWidth, const glm::vec4& color) {
    // Draw as 4 lines (screen-space line width)
    line(x, y, x + w, y, lineWidth, color);
    line(x + w, y, x + w, y + h, lineWidth, color);
    line(x + w, y + h, x, y + h, lineWidth, color);
    line(x, y + h, x, y, lineWidth, color);
}

void OverlayCanvas::fillCircle(float cx, float cy, float radius, const glm::vec4& color, int segments) {
    glm::vec2 center = transformPoint({cx, cy});
    uint32_t centerIndex = static_cast<uint32_t>(m_solidVertices.size());
    glm::vec2 uv(0.5f, 0.5f);

    m_solidVertices.push_back({center, uv, color});

    for (int i = 0; i <= segments; i++) {
        float angle = static_cast<float>(i) / segments * 2.0f * 3.14159265f;
        glm::vec2 p = transformPoint({cx + std::cos(angle) * radius, cy + std::sin(angle) * radius});
        m_solidVertices.push_back({p, uv, color});
    }

    for (int i = 0; i < segments; i++) {
        m_solidIndices.push_back(centerIndex);
        m_solidIndices.push_back(centerIndex + 1 + i);
        m_solidIndices.push_back(centerIndex + 2 + i);
    }
}

void OverlayCanvas::strokeCircle(float cx, float cy, float radius, float lineWidth, const glm::vec4& color, int segments) {
    for (int i = 0; i < segments; i++) {
        float angle0 = static_cast<float>(i) / segments * 2.0f * 3.14159265f;
        float angle1 = static_cast<float>(i + 1) / segments * 2.0f * 3.14159265f;

        float x0 = cx + std::cos(angle0) * radius;
        float y0 = cy + std::sin(angle0) * radius;
        float x1 = cx + std::cos(angle1) * radius;
        float y1 = cy + std::sin(angle1) * radius;

        line(x0, y0, x1, y1, lineWidth, color);
    }
}

void OverlayCanvas::line(float x1, float y1, float x2, float y2, float lineWidth, const glm::vec4& color) {
    // Transform endpoints
    glm::vec2 p1 = transformPoint({x1, y1});
    glm::vec2 p2 = transformPoint({x2, y2});

    // Calculate perpendicular (in screen space for consistent width)
    glm::vec2 dir = p2 - p1;
    float len = glm::length(dir);
    if (len < 0.001f) return;

    dir = dir / len;
    glm::vec2 perp(-dir.y, dir.x);
    float halfWidth = lineWidth * 0.5f;

    glm::vec2 v0 = p1 - perp * halfWidth;
    glm::vec2 v1 = p1 + perp * halfWidth;
    glm::vec2 v2 = p2 + perp * halfWidth;
    glm::vec2 v3 = p2 - perp * halfWidth;

    addQuad(v0, v1, v2, v3, color);
}

void OverlayCanvas::fillTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& color) {
    glm::vec2 uv(0.5f, 0.5f);
    uint32_t baseIndex = static_cast<uint32_t>(m_solidVertices.size());

    m_solidVertices.push_back({transformPoint(a), uv, color});
    m_solidVertices.push_back({transformPoint(b), uv, color});
    m_solidVertices.push_back({transformPoint(c), uv, color});

    m_solidIndices.push_back(baseIndex + 0);
    m_solidIndices.push_back(baseIndex + 1);
    m_solidIndices.push_back(baseIndex + 2);
}

void OverlayCanvas::bezierCurve(float x1, float y1, float cx1, float cy1,
                                 float cx2, float cy2, float x2, float y2,
                                 float lineWidth, const glm::vec4& color, int segments) {
    float prevX = x1, prevY = y1;

    for (int i = 1; i <= segments; i++) {
        float t = static_cast<float>(i) / segments;
        float t2 = t * t;
        float t3 = t2 * t;
        float mt = 1.0f - t;
        float mt2 = mt * mt;
        float mt3 = mt2 * mt;

        float x = mt3 * x1 + 3.0f * mt2 * t * cx1 + 3.0f * mt * t2 * cx2 + t3 * x2;
        float y = mt3 * y1 + 3.0f * mt2 * t * cy1 + 3.0f * mt * t2 * cy2 + t3 * y2;

        line(prevX, prevY, x, y, lineWidth, color);
        prevX = x;
        prevY = y;
    }
}

void OverlayCanvas::fillRoundedRect(float x, float y, float w, float h, float radius,
                                     const glm::vec4& color, int segments) {
    // Clamp radius
    radius = std::min(radius, std::min(w, h) * 0.5f);

    // Center rectangle
    fillRect(x + radius, y, w - 2 * radius, h, color);

    // Left and right rectangles
    fillRect(x, y + radius, radius, h - 2 * radius, color);
    fillRect(x + w - radius, y + radius, radius, h - 2 * radius, color);

    // Four corner arcs (as filled pie slices)
    auto drawCorner = [&](float cx, float cy, float startAngle) {
        glm::vec2 center = transformPoint({cx, cy});
        uint32_t centerIndex = static_cast<uint32_t>(m_solidVertices.size());
        glm::vec2 uv(0.5f, 0.5f);

        m_solidVertices.push_back({center, uv, color});

        for (int i = 0; i <= segments; i++) {
            float angle = startAngle + static_cast<float>(i) / segments * 1.5707963f;  // PI/2
            glm::vec2 p = transformPoint({cx + std::cos(angle) * radius, cy + std::sin(angle) * radius});
            m_solidVertices.push_back({p, uv, color});
        }

        for (int i = 0; i < segments; i++) {
            m_solidIndices.push_back(centerIndex);
            m_solidIndices.push_back(centerIndex + 1 + i);
            m_solidIndices.push_back(centerIndex + 2 + i);
        }
    };

    drawCorner(x + radius, y + radius, 3.14159265f);           // Top-left
    drawCorner(x + w - radius, y + radius, 4.71238898f);       // Top-right (3*PI/2)
    drawCorner(x + w - radius, y + h - radius, 0.0f);          // Bottom-right
    drawCorner(x + radius, y + h - radius, 1.5707963f);        // Bottom-left (PI/2)
}

void OverlayCanvas::strokeRoundedRect(float x, float y, float w, float h, float radius,
                                       float lineWidth, const glm::vec4& color, int segments) {
    radius = std::min(radius, std::min(w, h) * 0.5f);

    // Four straight edges
    line(x + radius, y, x + w - radius, y, lineWidth, color);           // Top
    line(x + w, y + radius, x + w, y + h - radius, lineWidth, color);   // Right
    line(x + w - radius, y + h, x + radius, y + h, lineWidth, color);   // Bottom
    line(x, y + h - radius, x, y + radius, lineWidth, color);           // Left

    // Four corner arcs
    auto drawCornerArc = [&](float cx, float cy, float startAngle) {
        for (int i = 0; i < segments; i++) {
            float a0 = startAngle + static_cast<float>(i) / segments * 1.5707963f;
            float a1 = startAngle + static_cast<float>(i + 1) / segments * 1.5707963f;
            line(cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
                 cx + std::cos(a1) * radius, cy + std::sin(a1) * radius,
                 lineWidth, color);
        }
    };

    drawCornerArc(x + radius, y + radius, 3.14159265f);           // Top-left
    drawCornerArc(x + w - radius, y + radius, 4.71238898f);       // Top-right
    drawCornerArc(x + w - radius, y + h - radius, 0.0f);          // Bottom-right
    drawCornerArc(x + radius, y + h - radius, 1.5707963f);        // Bottom-left
}

// -------------------------------------------------------------------------
// Topmost Layer (for tooltips)
// -------------------------------------------------------------------------

void OverlayCanvas::fillRoundedRectTopmost(float x, float y, float w, float h, float radius,
                                            const glm::vec4& color, int segments) {
    radius = std::min(radius, std::min(w, h) * 0.5f);
    glm::vec2 uv(0.5f, 0.5f);

    // Helper to add a quad to topmost layer
    auto addQuadTopmost = [&](glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3) {
        uint32_t baseIndex = static_cast<uint32_t>(m_topmostVertices.size());
        m_topmostVertices.push_back({p0, uv, color});
        m_topmostVertices.push_back({p1, uv, color});
        m_topmostVertices.push_back({p2, uv, color});
        m_topmostVertices.push_back({p3, uv, color});
        m_topmostIndices.push_back(baseIndex + 0);
        m_topmostIndices.push_back(baseIndex + 1);
        m_topmostIndices.push_back(baseIndex + 2);
        m_topmostIndices.push_back(baseIndex + 0);
        m_topmostIndices.push_back(baseIndex + 2);
        m_topmostIndices.push_back(baseIndex + 3);
    };

    // Center rectangle
    glm::vec2 p0 = transformPoint({x + radius, y});
    glm::vec2 p1 = transformPoint({x + w - radius, y});
    glm::vec2 p2 = transformPoint({x + w - radius, y + h});
    glm::vec2 p3 = transformPoint({x + radius, y + h});
    addQuadTopmost(p0, p1, p2, p3);

    // Left rectangle
    p0 = transformPoint({x, y + radius});
    p1 = transformPoint({x + radius, y + radius});
    p2 = transformPoint({x + radius, y + h - radius});
    p3 = transformPoint({x, y + h - radius});
    addQuadTopmost(p0, p1, p2, p3);

    // Right rectangle
    p0 = transformPoint({x + w - radius, y + radius});
    p1 = transformPoint({x + w, y + radius});
    p2 = transformPoint({x + w, y + h - radius});
    p3 = transformPoint({x + w - radius, y + h - radius});
    addQuadTopmost(p0, p1, p2, p3);

    // Four corner arcs
    auto drawCorner = [&](float cx, float cy, float startAngle) {
        glm::vec2 center = transformPoint({cx, cy});
        uint32_t centerIndex = static_cast<uint32_t>(m_topmostVertices.size());
        m_topmostVertices.push_back({center, uv, color});

        for (int i = 0; i <= segments; i++) {
            float angle = startAngle + static_cast<float>(i) / segments * 1.5707963f;
            glm::vec2 p = transformPoint({cx + std::cos(angle) * radius, cy + std::sin(angle) * radius});
            m_topmostVertices.push_back({p, uv, color});
        }

        for (int i = 0; i < segments; i++) {
            m_topmostIndices.push_back(centerIndex);
            m_topmostIndices.push_back(centerIndex + 1 + i);
            m_topmostIndices.push_back(centerIndex + 2 + i);
        }
    };

    drawCorner(x + radius, y + radius, 3.14159265f);
    drawCorner(x + w - radius, y + radius, 4.71238898f);
    drawCorner(x + w - radius, y + h - radius, 0.0f);
    drawCorner(x + radius, y + h - radius, 1.5707963f);
}

void OverlayCanvas::strokeRoundedRectTopmost(float x, float y, float w, float h, float radius,
                                              float lineWidth, const glm::vec4& color, int segments) {
    radius = std::min(radius, std::min(w, h) * 0.5f);
    glm::vec2 uv(0.5f, 0.5f);

    // Helper to draw a line in topmost layer
    auto lineTopmost = [&](float x1, float y1, float x2, float y2) {
        glm::vec2 p1t = transformPoint({x1, y1});
        glm::vec2 p2t = transformPoint({x2, y2});
        glm::vec2 dir = p2t - p1t;
        float len = glm::length(dir);
        if (len < 0.001f) return;
        dir = dir / len;
        glm::vec2 perp(-dir.y, dir.x);
        float halfWidth = lineWidth * 0.5f;

        glm::vec2 v0 = p1t - perp * halfWidth;
        glm::vec2 v1 = p1t + perp * halfWidth;
        glm::vec2 v2 = p2t + perp * halfWidth;
        glm::vec2 v3 = p2t - perp * halfWidth;

        uint32_t baseIndex = static_cast<uint32_t>(m_topmostVertices.size());
        m_topmostVertices.push_back({v0, uv, color});
        m_topmostVertices.push_back({v1, uv, color});
        m_topmostVertices.push_back({v2, uv, color});
        m_topmostVertices.push_back({v3, uv, color});
        m_topmostIndices.push_back(baseIndex + 0);
        m_topmostIndices.push_back(baseIndex + 1);
        m_topmostIndices.push_back(baseIndex + 2);
        m_topmostIndices.push_back(baseIndex + 0);
        m_topmostIndices.push_back(baseIndex + 2);
        m_topmostIndices.push_back(baseIndex + 3);
    };

    // Four straight edges
    lineTopmost(x + radius, y, x + w - radius, y);
    lineTopmost(x + w, y + radius, x + w, y + h - radius);
    lineTopmost(x + w - radius, y + h, x + radius, y + h);
    lineTopmost(x, y + h - radius, x, y + radius);

    // Four corner arcs
    auto drawCornerArc = [&](float cx, float cy, float startAngle) {
        for (int i = 0; i < segments; i++) {
            float a0 = startAngle + static_cast<float>(i) / segments * 1.5707963f;
            float a1 = startAngle + static_cast<float>(i + 1) / segments * 1.5707963f;
            lineTopmost(cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
                        cx + std::cos(a1) * radius, cy + std::sin(a1) * radius);
        }
    };

    drawCornerArc(x + radius, y + radius, 3.14159265f);
    drawCornerArc(x + w - radius, y + radius, 4.71238898f);
    drawCornerArc(x + w - radius, y + h - radius, 0.0f);
    drawCornerArc(x + radius, y + h - radius, 1.5707963f);
}

void OverlayCanvas::textTopmost(const std::string& str, float x, float y, const glm::vec4& color, int fontIndex) {
    if (fontIndex < 0 || fontIndex >= 3 || !m_fonts[fontIndex]) return;

    FontAtlas& font = *m_fonts[fontIndex];
    float cursorX = x;
    float cursorY = y;
    char prevChar = 0;

    for (char c : str) {
        if (c == '\n') {
            cursorX = x;
            cursorY += font.lineHeight();
            prevChar = 0;
            continue;
        }

        const GlyphInfo* glyph = font.getGlyph(c);
        if (!glyph) continue;

        if (prevChar != 0) {
            cursorX += font.getKerning(prevChar, c);
        }

        float x0 = cursorX + glyph->xoff;
        float y0 = cursorY + glyph->yoff;
        float x1 = x0 + glyph->width;
        float y1 = y0 + glyph->height;

        glm::vec2 p0 = transformPoint({x0, y0});
        glm::vec2 p1 = transformPoint({x1, y0});
        glm::vec2 p2 = transformPoint({x1, y1});
        glm::vec2 p3 = transformPoint({x0, y1});

        // Add to topmost text batch
        uint32_t baseIndex = static_cast<uint32_t>(m_topmostTextVertices[fontIndex].size());
        m_topmostTextVertices[fontIndex].push_back({p0, {glyph->u0, glyph->v0}, color});
        m_topmostTextVertices[fontIndex].push_back({p1, {glyph->u1, glyph->v0}, color});
        m_topmostTextVertices[fontIndex].push_back({p2, {glyph->u1, glyph->v1}, color});
        m_topmostTextVertices[fontIndex].push_back({p3, {glyph->u0, glyph->v1}, color});

        m_topmostTextIndices[fontIndex].push_back(baseIndex + 0);
        m_topmostTextIndices[fontIndex].push_back(baseIndex + 1);
        m_topmostTextIndices[fontIndex].push_back(baseIndex + 2);
        m_topmostTextIndices[fontIndex].push_back(baseIndex + 0);
        m_topmostTextIndices[fontIndex].push_back(baseIndex + 2);
        m_topmostTextIndices[fontIndex].push_back(baseIndex + 3);

        cursorX += glyph->xadvance;
        prevChar = c;
    }
}

// -------------------------------------------------------------------------
// Text
// -------------------------------------------------------------------------

void OverlayCanvas::text(const std::string& str, float x, float y, const glm::vec4& color, int fontIndex) {
    textScaled(str, x, y, color, 1.0f, fontIndex);
}

void OverlayCanvas::textScaled(const std::string& str, float x, float y, const glm::vec4& color, float scale, int fontIndex) {
    if (fontIndex < 0 || fontIndex >= 3 || !m_fonts[fontIndex]) return;

    FontAtlas& font = *m_fonts[fontIndex];
    float cursorX = x;
    float cursorY = y;
    char prevChar = 0;

    for (char c : str) {
        if (c == '\n') {
            cursorX = x;
            cursorY += font.lineHeight() * scale;
            prevChar = 0;
            continue;
        }

        const GlyphInfo* glyph = font.getGlyph(c);
        if (!glyph) continue;

        if (prevChar != 0) {
            cursorX += font.getKerning(prevChar, c) * scale;
        }

        // Scale glyph dimensions by scale factor
        float x0 = cursorX + glyph->xoff * scale;
        float y0 = cursorY + glyph->yoff * scale;
        float x1 = x0 + glyph->width * scale;
        float y1 = y0 + glyph->height * scale;

        // Transform glyph corners
        glm::vec2 p0 = transformPoint({x0, y0});
        glm::vec2 p1 = transformPoint({x1, y0});
        glm::vec2 p2 = transformPoint({x1, y1});
        glm::vec2 p3 = transformPoint({x0, y1});

        addTextQuad(p0, p1, p2, p3,
                    {glyph->u0, glyph->v0}, {glyph->u1, glyph->v0},
                    {glyph->u1, glyph->v1}, {glyph->u0, glyph->v1},
                    color, fontIndex);

        cursorX += glyph->xadvance * scale;
        prevChar = c;
    }
}

float OverlayCanvas::measureText(const std::string& str, int fontIndex) const {
    if (fontIndex < 0 || fontIndex >= 3 || !m_fonts[fontIndex]) return 0.0f;

    FontAtlas& font = *m_fonts[fontIndex];
    float width = 0.0f;
    char prevChar = 0;

    for (char c : str) {
        if (c == '\n') {
            prevChar = 0;
            continue;
        }

        const GlyphInfo* glyph = font.getGlyph(c);
        if (!glyph) continue;

        if (prevChar != 0) {
            width += font.getKerning(prevChar, c);
        }

        width += glyph->xadvance;
        prevChar = c;
    }

    return width;
}

float OverlayCanvas::measureTextScaled(const std::string& str, float scale, int fontIndex) const {
    return measureText(str, fontIndex) * scale;
}

int OverlayCanvas::getFontForZoom(float zoom) const {
    // Always use base font - text scales with nodes via position/size calculations
    // Bitmap fonts don't scale smoothly, so we accept some pixelation at extreme zoom
    (void)zoom;  // Unused
    return 0;
}

float OverlayCanvas::fontLineHeight(int fontIndex) const {
    if (fontIndex < 0 || fontIndex >= 3 || !m_fonts[fontIndex]) return 0.0f;
    return m_fonts[fontIndex]->lineHeight();
}

float OverlayCanvas::fontAscent(int fontIndex) const {
    if (fontIndex < 0 || fontIndex >= 3 || !m_fonts[fontIndex]) return 0.0f;
    return m_fonts[fontIndex]->ascent();
}

float OverlayCanvas::fontDescent(int fontIndex) const {
    if (fontIndex < 0 || fontIndex >= 3 || !m_fonts[fontIndex]) return 0.0f;
    return m_fonts[fontIndex]->descent();
}

float OverlayCanvas::fontSize(int fontIndex) const {
    if (fontIndex < 0 || fontIndex >= 3 || !m_fonts[fontIndex]) return 0.0f;
    return m_fonts[fontIndex]->fontSize();
}

// -------------------------------------------------------------------------
// Utilities
// -------------------------------------------------------------------------

int OverlayCanvas::getCircleSegments(float radius, float zoom) {
    float screenRadius = radius * zoom;
    return std::clamp(static_cast<int>(screenRadius * 0.6f), 8, 128);
}

} // namespace vivid
