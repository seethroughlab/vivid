#include <vivid/gui/overlay_canvas.h>
#include <vivid/asset_loader.h>
#include "effects/font_atlas.h"  // For backward compatibility
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <set>

namespace vivid {

// External shader loaded at runtime
static std::string s_overlayShader;

static void ensureOverlayShaderLoaded() {
    if (s_overlayShader.empty()) {
        s_overlayShader = AssetLoader::instance().loadShader("overlay.wgsl");
    }
}

// Shader for overlay rendering (no stencil, simple alpha blending) - fallback
static const char* OVERLAY_SHADER_FALLBACK = R"(
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
    // Release font bind groups (we don't own the FontProvider pointers)
    for (int i = 0; i < 3; i++) {
        if (m_fontBindGroups[i]) {
            wgpuBindGroupRelease(m_fontBindGroups[i]);
            m_fontBindGroups[i] = nullptr;
        }
        m_fonts[i] = nullptr;
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

bool OverlayCanvas::init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surfaceFormat) {
    if (m_initialized) return true;

    m_device = device;
    m_queue = queue;
    m_surfaceFormat = surfaceFormat;

    createPipeline();
    createWhiteTexture();

    m_initialized = true;
    return true;
}

void OverlayCanvas::createPipeline() {
    WGPUDevice device = m_device;

    ensureOverlayShaderLoaded();
    const std::string& shaderSource = s_overlayShader.empty() ? OVERLAY_SHADER_FALLBACK : s_overlayShader;

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource.c_str());

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

void OverlayCanvas::createWhiteTexture() {
    WGPUDevice device = m_device;
    WGPUQueue queue = m_queue;

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

void OverlayCanvas::setFont(int index, FontProvider* provider) {
    if (index < 0 || index >= 3) return;

    m_fonts[index] = provider;

    // Release old bind group
    if (m_fontBindGroups[index]) {
        wgpuBindGroupRelease(m_fontBindGroups[index]);
        m_fontBindGroups[index] = nullptr;
    }

    // Create bind group for this font if provider is valid
    if (provider && provider->valid() && m_device) {
        WGPUBindGroupEntry bgEntries[3] = {};
        bgEntries[0].binding = 0;
        bgEntries[0].buffer = m_uniformBuffer;
        bgEntries[0].size = 16;
        bgEntries[1].binding = 1;
        bgEntries[1].sampler = m_sampler;
        bgEntries[2].binding = 2;
        bgEntries[2].textureView = provider->textureView();

        WGPUBindGroupDescriptor bgDesc = {};
        bgDesc.layout = m_bindGroupLayout;
        bgDesc.entryCount = 3;
        bgDesc.entries = bgEntries;
        m_fontBindGroups[index] = wgpuDeviceCreateBindGroup(m_device, &bgDesc);
    }
}

FontProvider* OverlayCanvas::getFont(int index) const {
    if (index < 0 || index >= 3) return nullptr;
    return m_fonts[index];
}

void OverlayCanvas::begin(int width, int height) {
    m_width = width;
    m_height = height;

    // Clear all per-layer batched geometry
    m_layers.clear();
    m_texturedRects.clear();

    // Reset to default layer
    m_currentLayer = 0;

    // Clear clip stack and reset tracking state
    m_clipStack.clear();
    m_lastHasClip = false;
    m_lastClipRect = {0, 0, 0, 0};

    // Clear per-layer clip rects
    m_layerClipRects.clear();

    // Reset transform
    m_transform = glm::mat3(1.0f);
    m_transformStack.clear();
}

void OverlayCanvas::setLayer(int layer) {
    m_currentLayer = layer;
}

void OverlayCanvas::render(WGPURenderPassEncoder pass) {
    if (!m_initialized) return;

    // Check if any content exists
    if (m_layers.empty() && m_texturedRects.empty()) return;

    // Update uniforms
    float uniforms[4] = {static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 0.0f};
    wgpuQueueWriteBuffer(m_queue, m_uniformBuffer, 0, uniforms, sizeof(uniforms));

    // Set pipeline
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);

    // Collect all unique layers (map is already sorted by key)
    std::set<int> allLayers;
    for (const auto& [layer, _] : m_layers) allLayers.insert(layer);
    for (const auto& [layer, _] : m_texturedRects) allLayers.insert(layer);

    // =======================================================================
    // Accumulate ALL content from all layers into combined buffers.
    // Track segments with clip info for scissor clipping.
    // =======================================================================

    // Segment with adjusted indices for combined buffer
    struct CombinedSegment {
        uint32_t startIndex;
        uint32_t indexCount;
        bool hasClip;
        ClipRect clipRect;
        int layer;
    };
    std::vector<CombinedSegment> allSolidSegments;
    std::vector<CombinedSegment> allTextSegments[3];  // Per-font

    // Accumulate solid primitives from all layers
    std::vector<OverlayVertex> allSolidVertices;
    std::vector<uint32_t> allSolidIndices;
    for (int layer : allLayers) {
        auto layerIt = m_layers.find(layer);
        if (layerIt == m_layers.end() || layerIt->second.solidVertices.empty()) continue;

        const auto& batch = layerIt->second;
        uint32_t vertexOffset = static_cast<uint32_t>(allSolidVertices.size());
        uint32_t indexOffset = static_cast<uint32_t>(allSolidIndices.size());

        // Copy vertices
        allSolidVertices.insert(allSolidVertices.end(),
                                batch.solidVertices.begin(), batch.solidVertices.end());

        // Copy indices with vertex offset adjustment
        for (uint32_t idx : batch.solidIndices) {
            allSolidIndices.push_back(idx + vertexOffset);
        }

        // Copy segments with index offset adjustment
        // If the layer has content but no segments (edge case), create a default segment
        if (batch.solidSegments.empty() && !batch.solidIndices.empty()) {
            CombinedSegment combined;
            combined.startIndex = indexOffset;
            combined.indexCount = static_cast<uint32_t>(batch.solidIndices.size());
            combined.hasClip = false;
            combined.clipRect = {0, 0, 0, 0};
            combined.layer = layer;
            allSolidSegments.push_back(combined);
        } else {
            for (const auto& seg : batch.solidSegments) {
                CombinedSegment combined;
                combined.startIndex = seg.startIndex + indexOffset;
                combined.indexCount = seg.indexCount;
                combined.hasClip = seg.hasClip;
                combined.clipRect = seg.clipRect;
                combined.layer = layer;
                allSolidSegments.push_back(combined);
            }
        }
    }

    // Accumulate text from all layers (per font)
    std::vector<OverlayVertex> allTextVertices[3];
    std::vector<uint32_t> allTextIndices[3];
    for (int fontIdx = 0; fontIdx < 3; fontIdx++) {
        for (int layer : allLayers) {
            auto layerIt = m_layers.find(layer);
            if (layerIt == m_layers.end() || layerIt->second.textVertices[fontIdx].empty()) continue;

            const auto& batch = layerIt->second;
            uint32_t vertexOffset = static_cast<uint32_t>(allTextVertices[fontIdx].size());
            uint32_t indexOffset = static_cast<uint32_t>(allTextIndices[fontIdx].size());

            allTextVertices[fontIdx].insert(allTextVertices[fontIdx].end(),
                                            batch.textVertices[fontIdx].begin(),
                                            batch.textVertices[fontIdx].end());

            for (uint32_t idx : batch.textIndices[fontIdx]) {
                allTextIndices[fontIdx].push_back(idx + vertexOffset);
            }

            // Copy segments with index offset adjustment
            // If the layer has content but no segments (edge case), create a default segment
            if (batch.textSegments[fontIdx].empty() && !batch.textIndices[fontIdx].empty()) {
                CombinedSegment combined;
                combined.startIndex = indexOffset;
                combined.indexCount = static_cast<uint32_t>(batch.textIndices[fontIdx].size());
                combined.hasClip = false;
                combined.clipRect = {0, 0, 0, 0};
                combined.layer = layer;
                allTextSegments[fontIdx].push_back(combined);
            } else {
                for (const auto& seg : batch.textSegments[fontIdx]) {
                    CombinedSegment combined;
                    combined.startIndex = seg.startIndex + indexOffset;
                    combined.indexCount = seg.indexCount;
                    combined.hasClip = seg.hasClip;
                    combined.clipRect = seg.clipRect;
                    combined.layer = layer;
                    allTextSegments[fontIdx].push_back(combined);
                }
            }
        }
    }

    // =======================================================================
    // Upload combined buffers ONCE
    // =======================================================================

    // Upload solid primitives (all layers combined)
    if (!allSolidVertices.empty()) {
        size_t neededVertexSize = allSolidVertices.size() * sizeof(OverlayVertex);
        size_t neededIndexSize = allSolidIndices.size() * sizeof(uint32_t);

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

        wgpuQueueWriteBuffer(m_queue, m_solidVertexBuffer, 0, allSolidVertices.data(), neededVertexSize);
        wgpuQueueWriteBuffer(m_queue, m_solidIndexBuffer, 0, allSolidIndices.data(), neededIndexSize);
    }

    // Upload text (all layers combined, per font)
    for (int fontIdx = 0; fontIdx < 3; fontIdx++) {
        if (allTextVertices[fontIdx].empty() || !m_fontBindGroups[fontIdx]) continue;

        size_t neededVertexSize = allTextVertices[fontIdx].size() * sizeof(OverlayVertex);
        size_t neededIndexSize = allTextIndices[fontIdx].size() * sizeof(uint32_t);

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

        wgpuQueueWriteBuffer(m_queue, m_textVertexBuffer[fontIdx], 0,
                            allTextVertices[fontIdx].data(), neededVertexSize);
        wgpuQueueWriteBuffer(m_queue, m_textIndexBuffer[fontIdx], 0,
                            allTextIndices[fontIdx].data(), neededIndexSize);
    }

    // =======================================================================
    // Render each layer in order: solids -> textured rects -> text
    // Apply scissor rects per segment for clipping
    // =======================================================================

    // Helper to apply scissor rect
    auto applyScissor = [&](const CombinedSegment& seg) {
        if (seg.hasClip) {
            // Clamp to viewport bounds and ensure positive dimensions
            uint32_t x = static_cast<uint32_t>(std::max(0.0f, seg.clipRect.x));
            uint32_t y = static_cast<uint32_t>(std::max(0.0f, seg.clipRect.y));
            uint32_t w = static_cast<uint32_t>(std::max(0.0f, seg.clipRect.w));
            uint32_t h = static_cast<uint32_t>(std::max(0.0f, seg.clipRect.h));
            // Clamp to viewport
            if (x + w > static_cast<uint32_t>(m_width)) w = static_cast<uint32_t>(m_width) - x;
            if (y + h > static_cast<uint32_t>(m_height)) h = static_cast<uint32_t>(m_height) - y;
            // Only set scissor if we have valid dimensions
            if (w > 0 && h > 0) {
                wgpuRenderPassEncoderSetScissorRect(pass, x, y, w, h);
            }
        } else {
            // Reset to full viewport
            wgpuRenderPassEncoderSetScissorRect(pass, 0, 0,
                                                 static_cast<uint32_t>(m_width),
                                                 static_cast<uint32_t>(m_height));
        }
    };

    // Track segment indices
    size_t solidSegIdx = 0;
    size_t textSegIdx[3] = {0, 0, 0};

    for (int layer : allLayers) {
        // Check if this layer has a clip rect
        auto layerClipIt = m_layerClipRects.find(layer);
        bool hasLayerClip = (layerClipIt != m_layerClipRects.end());
        if (hasLayerClip) {
            const auto& clip = layerClipIt->second;
            uint32_t x = static_cast<uint32_t>(std::max(0.0f, clip.x));
            uint32_t y = static_cast<uint32_t>(std::max(0.0f, clip.y));
            uint32_t w = static_cast<uint32_t>(std::max(0.0f, clip.w));
            uint32_t h = static_cast<uint32_t>(std::max(0.0f, clip.h));
            if (x + w > static_cast<uint32_t>(m_width)) w = static_cast<uint32_t>(m_width) - x;
            if (y + h > static_cast<uint32_t>(m_height)) h = static_cast<uint32_t>(m_height) - y;
            if (w > 0 && h > 0) {
                wgpuRenderPassEncoderSetScissorRect(pass, x, y, w, h);
            }
        } else {
            // Reset to full viewport for layers without clip
            wgpuRenderPassEncoderSetScissorRect(pass, 0, 0,
                                                 static_cast<uint32_t>(m_width),
                                                 static_cast<uint32_t>(m_height));
        }

        // 1. Render solid segments for this layer
        while (solidSegIdx < allSolidSegments.size() && allSolidSegments[solidSegIdx].layer == layer) {
            const auto& seg = allSolidSegments[solidSegIdx];
            if (seg.indexCount > 0) {
                // Only apply segment scissor if no layer clip (layer clip takes precedence)
                if (!hasLayerClip) {
                    applyScissor(seg);
                }
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_whiteBindGroup, 0, nullptr);
                wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_solidVertexBuffer, 0,
                                                      allSolidVertices.size() * sizeof(OverlayVertex));
                wgpuRenderPassEncoderSetIndexBuffer(pass, m_solidIndexBuffer, WGPUIndexFormat_Uint32, 0,
                                                     allSolidIndices.size() * sizeof(uint32_t));
                wgpuRenderPassEncoderDrawIndexed(pass, seg.indexCount, 1, seg.startIndex, 0, 0);
            }
            solidSegIdx++;
        }

        // 2. Render textured rects for this layer
        auto texIt = m_texturedRects.find(layer);
        if (texIt != m_texturedRects.end() && !texIt->second.empty()) {
            // Only reset scissor if no layer clip (layer clip is already set above)
            if (!hasLayerClip) {
                wgpuRenderPassEncoderSetScissorRect(pass, 0, 0,
                                                     static_cast<uint32_t>(m_width),
                                                     static_cast<uint32_t>(m_height));
            }

            const auto& rects = texIt->second;

            std::vector<OverlayVertex> texVertices;
            std::vector<uint32_t> texIndices;
            std::vector<WGPUBindGroup> tempBindGroups;
            texVertices.reserve(rects.size() * 4);
            texIndices.reserve(rects.size() * 6);

            for (const auto& rect : rects) {
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
                tempBindGroups.push_back(wgpuDeviceCreateBindGroup(m_device, &bgDesc));
            }

            if (!texVertices.empty()) {
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

                for (size_t i = 0; i < rects.size(); i++) {
                    wgpuRenderPassEncoderSetBindGroup(pass, 0, tempBindGroups[i], 0, nullptr);
                    wgpuRenderPassEncoderDrawIndexed(pass, 6, 1, static_cast<uint32_t>(i * 6), 0, 0);
                }

                wgpuBufferRelease(vertexBuffer);
                wgpuBufferRelease(indexBuffer);
            }

            for (auto bg : tempBindGroups) wgpuBindGroupRelease(bg);
        }

        // 3. Render text segments for each font at this layer
        for (int fontIdx = 0; fontIdx < 3; fontIdx++) {
            while (textSegIdx[fontIdx] < allTextSegments[fontIdx].size() &&
                   allTextSegments[fontIdx][textSegIdx[fontIdx]].layer == layer) {
                const auto& seg = allTextSegments[fontIdx][textSegIdx[fontIdx]];
                if (seg.indexCount > 0) {
                    // Only apply segment scissor if no layer clip (layer clip takes precedence)
                    if (!hasLayerClip) {
                        applyScissor(seg);
                    }
                    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_fontBindGroups[fontIdx], 0, nullptr);
                    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_textVertexBuffer[fontIdx], 0,
                                                          allTextVertices[fontIdx].size() * sizeof(OverlayVertex));
                    wgpuRenderPassEncoderSetIndexBuffer(pass, m_textIndexBuffer[fontIdx], WGPUIndexFormat_Uint32, 0,
                                                         allTextIndices[fontIdx].size() * sizeof(uint32_t));
                    wgpuRenderPassEncoderDrawIndexed(pass, seg.indexCount, 1, seg.startIndex, 0, 0);
                }
                textSegIdx[fontIdx]++;
            }
        }
    }

    // Reset scissor to full viewport at the end
    wgpuRenderPassEncoderSetScissorRect(pass, 0, 0,
                                         static_cast<uint32_t>(m_width),
                                         static_cast<uint32_t>(m_height));
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
    auto& batch = m_layers[m_currentLayer];

    // Check current clip state
    bool hasClip = !m_clipStack.empty();
    ClipRect currentClip = hasClip ? m_clipStack.back() : ClipRect{0, 0, 0, 0};

    // Check if we need to start a new segment (clip state changed)
    bool needNewSegment = batch.solidSegments.empty() ||
                          hasClip != m_lastHasClip ||
                          (hasClip && (currentClip.x != m_lastClipRect.x ||
                                       currentClip.y != m_lastClipRect.y ||
                                       currentClip.w != m_lastClipRect.w ||
                                       currentClip.h != m_lastClipRect.h));

    if (needNewSegment) {
        // Start new segment
        DrawSegment seg;
        seg.startIndex = static_cast<uint32_t>(batch.solidIndices.size());
        seg.indexCount = 0;
        seg.hasClip = hasClip;
        seg.clipRect = currentClip;
        batch.solidSegments.push_back(seg);
        m_lastHasClip = hasClip;
        m_lastClipRect = currentClip;
    }

    uint32_t baseIndex = static_cast<uint32_t>(batch.solidVertices.size());
    glm::vec2 uv(0.5f, 0.5f);

    batch.solidVertices.push_back({p0, uv, color});
    batch.solidVertices.push_back({p1, uv, color});
    batch.solidVertices.push_back({p2, uv, color});
    batch.solidVertices.push_back({p3, uv, color});

    batch.solidIndices.push_back(baseIndex + 0);
    batch.solidIndices.push_back(baseIndex + 1);
    batch.solidIndices.push_back(baseIndex + 2);
    batch.solidIndices.push_back(baseIndex + 0);
    batch.solidIndices.push_back(baseIndex + 2);
    batch.solidIndices.push_back(baseIndex + 3);

    // Update current segment's index count
    batch.solidSegments.back().indexCount += 6;
}

void OverlayCanvas::addTextQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                                 glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3,
                                 const glm::vec4& color, int fontIndex) {
    if (fontIndex < 0 || fontIndex >= 3) fontIndex = 0;
    auto& batch = m_layers[m_currentLayer];

    // Check current clip state
    bool hasClip = !m_clipStack.empty();
    ClipRect currentClip = hasClip ? m_clipStack.back() : ClipRect{0, 0, 0, 0};

    // Check if we need to start a new segment (clip state changed)
    bool needNewSegment = batch.textSegments[fontIndex].empty() ||
                          hasClip != m_lastHasClip ||
                          (hasClip && (currentClip.x != m_lastClipRect.x ||
                                       currentClip.y != m_lastClipRect.y ||
                                       currentClip.w != m_lastClipRect.w ||
                                       currentClip.h != m_lastClipRect.h));

    if (needNewSegment) {
        // Start new segment
        DrawSegment seg;
        seg.startIndex = static_cast<uint32_t>(batch.textIndices[fontIndex].size());
        seg.indexCount = 0;
        seg.hasClip = hasClip;
        seg.clipRect = currentClip;
        batch.textSegments[fontIndex].push_back(seg);
        m_lastHasClip = hasClip;
        m_lastClipRect = currentClip;
    }

    uint32_t baseIndex = static_cast<uint32_t>(batch.textVertices[fontIndex].size());

    batch.textVertices[fontIndex].push_back({p0, uv0, color});
    batch.textVertices[fontIndex].push_back({p1, uv1, color});
    batch.textVertices[fontIndex].push_back({p2, uv2, color});
    batch.textVertices[fontIndex].push_back({p3, uv3, color});

    batch.textIndices[fontIndex].push_back(baseIndex + 0);
    batch.textIndices[fontIndex].push_back(baseIndex + 1);
    batch.textIndices[fontIndex].push_back(baseIndex + 2);
    batch.textIndices[fontIndex].push_back(baseIndex + 0);
    batch.textIndices[fontIndex].push_back(baseIndex + 2);
    batch.textIndices[fontIndex].push_back(baseIndex + 3);

    // Update current segment's index count
    batch.textSegments[fontIndex].back().indexCount += 6;
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

    // Store for deferred rendering at current layer
    TexturedRect rect;
    rect.pos = p0;
    rect.size = p1 - p0;
    rect.textureView = textureView;
    rect.tint = tint;
    m_texturedRects[m_currentLayer].push_back(rect);
}

void OverlayCanvas::strokeRect(float x, float y, float w, float h, float lineWidth, const glm::vec4& color) {
    // Draw as 4 lines (screen-space line width)
    line(x, y, x + w, y, lineWidth, color);
    line(x + w, y, x + w, y + h, lineWidth, color);
    line(x + w, y + h, x, y + h, lineWidth, color);
    line(x, y + h, x, y, lineWidth, color);
}

void OverlayCanvas::fillCircle(float cx, float cy, float radius, const glm::vec4& color, int segments) {
    auto& batch = m_layers[m_currentLayer];

    // Check current clip state and ensure segment tracking
    bool hasClip = !m_clipStack.empty();
    ClipRect currentClip = hasClip ? m_clipStack.back() : ClipRect{0, 0, 0, 0};

    bool needNewSegment = batch.solidSegments.empty() ||
                          hasClip != m_lastHasClip ||
                          (hasClip && (currentClip.x != m_lastClipRect.x ||
                                       currentClip.y != m_lastClipRect.y ||
                                       currentClip.w != m_lastClipRect.w ||
                                       currentClip.h != m_lastClipRect.h));

    if (needNewSegment) {
        DrawSegment seg;
        seg.startIndex = static_cast<uint32_t>(batch.solidIndices.size());
        seg.indexCount = 0;
        seg.hasClip = hasClip;
        seg.clipRect = currentClip;
        batch.solidSegments.push_back(seg);
        m_lastHasClip = hasClip;
        m_lastClipRect = currentClip;
    }

    glm::vec2 center = transformPoint({cx, cy});
    uint32_t centerIndex = static_cast<uint32_t>(batch.solidVertices.size());
    glm::vec2 uv(0.5f, 0.5f);

    batch.solidVertices.push_back({center, uv, color});

    for (int i = 0; i <= segments; i++) {
        float angle = static_cast<float>(i) / segments * 2.0f * 3.14159265f;
        glm::vec2 p = transformPoint({cx + std::cos(angle) * radius, cy + std::sin(angle) * radius});
        batch.solidVertices.push_back({p, uv, color});
    }

    for (int i = 0; i < segments; i++) {
        batch.solidIndices.push_back(centerIndex);
        batch.solidIndices.push_back(centerIndex + 1 + i);
        batch.solidIndices.push_back(centerIndex + 2 + i);
    }

    // Update segment index count
    batch.solidSegments.back().indexCount += segments * 3;
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
    auto& batch = m_layers[m_currentLayer];
    glm::vec2 uv(0.5f, 0.5f);
    uint32_t baseIndex = static_cast<uint32_t>(batch.solidVertices.size());

    batch.solidVertices.push_back({transformPoint(a), uv, color});
    batch.solidVertices.push_back({transformPoint(b), uv, color});
    batch.solidVertices.push_back({transformPoint(c), uv, color});

    batch.solidIndices.push_back(baseIndex + 0);
    batch.solidIndices.push_back(baseIndex + 1);
    batch.solidIndices.push_back(baseIndex + 2);
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
        auto& batch = m_layers[m_currentLayer];

        // Check current clip state and ensure segment tracking
        bool hasClip = !m_clipStack.empty();
        ClipRect currentClip = hasClip ? m_clipStack.back() : ClipRect{0, 0, 0, 0};

        bool needNewSegment = batch.solidSegments.empty() ||
                              hasClip != m_lastHasClip ||
                              (hasClip && (currentClip.x != m_lastClipRect.x ||
                                           currentClip.y != m_lastClipRect.y ||
                                           currentClip.w != m_lastClipRect.w ||
                                           currentClip.h != m_lastClipRect.h));

        if (needNewSegment) {
            DrawSegment seg;
            seg.startIndex = static_cast<uint32_t>(batch.solidIndices.size());
            seg.indexCount = 0;
            seg.hasClip = hasClip;
            seg.clipRect = currentClip;
            batch.solidSegments.push_back(seg);
            m_lastHasClip = hasClip;
            m_lastClipRect = currentClip;
        }

        glm::vec2 center = transformPoint({cx, cy});
        uint32_t centerIndex = static_cast<uint32_t>(batch.solidVertices.size());
        glm::vec2 uv(0.5f, 0.5f);

        batch.solidVertices.push_back({center, uv, color});

        for (int i = 0; i <= segments; i++) {
            float angle = startAngle + static_cast<float>(i) / segments * 1.5707963f;  // PI/2
            glm::vec2 p = transformPoint({cx + std::cos(angle) * radius, cy + std::sin(angle) * radius});
            batch.solidVertices.push_back({p, uv, color});
        }

        for (int i = 0; i < segments; i++) {
            batch.solidIndices.push_back(centerIndex);
            batch.solidIndices.push_back(centerIndex + 1 + i);
            batch.solidIndices.push_back(centerIndex + 2 + i);
        }

        // Update segment index count
        batch.solidSegments.back().indexCount += segments * 3;
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

void OverlayCanvas::fillRoundedRectTop(float x, float y, float w, float h, float radius,
                                        const glm::vec4& color, int segments) {
    // Clamp radius to half the smaller dimension
    radius = std::min(radius, std::min(w, h) * 0.5f);

    // Main body: full width rectangle below top corners
    fillRect(x, y + radius, w, h - radius, color);

    // Top center: rectangle between corners
    fillRect(x + radius, y, w - 2 * radius, radius, color);

    // Top-left and top-right corner arcs
    auto drawCorner = [&](float cx, float cy, float startAngle) {
        auto& batch = m_layers[m_currentLayer];

        // Check current clip state and ensure segment tracking
        bool hasClip = !m_clipStack.empty();
        ClipRect currentClip = hasClip ? m_clipStack.back() : ClipRect{0, 0, 0, 0};

        bool needNewSegment = batch.solidSegments.empty() ||
                              hasClip != m_lastHasClip ||
                              (hasClip && (currentClip.x != m_lastClipRect.x ||
                                           currentClip.y != m_lastClipRect.y ||
                                           currentClip.w != m_lastClipRect.w ||
                                           currentClip.h != m_lastClipRect.h));

        if (needNewSegment) {
            DrawSegment seg;
            seg.startIndex = static_cast<uint32_t>(batch.solidIndices.size());
            seg.indexCount = 0;
            seg.hasClip = hasClip;
            seg.clipRect = currentClip;
            batch.solidSegments.push_back(seg);
            m_lastHasClip = hasClip;
            m_lastClipRect = currentClip;
        }

        glm::vec2 center = transformPoint({cx, cy});
        uint32_t centerIndex = static_cast<uint32_t>(batch.solidVertices.size());
        glm::vec2 uv(0.5f, 0.5f);

        batch.solidVertices.push_back({center, uv, color});

        for (int i = 0; i <= segments; i++) {
            float angle = startAngle + static_cast<float>(i) / segments * 1.5707963f;  // PI/2
            glm::vec2 p = transformPoint({cx + std::cos(angle) * radius, cy + std::sin(angle) * radius});
            batch.solidVertices.push_back({p, uv, color});
        }

        for (int i = 0; i < segments; i++) {
            batch.solidIndices.push_back(centerIndex);
            batch.solidIndices.push_back(centerIndex + 1 + i);
            batch.solidIndices.push_back(centerIndex + 2 + i);
        }

        // Update segment index count
        batch.solidSegments.back().indexCount += segments * 3;
    };

    drawCorner(x + radius, y + radius, 3.14159265f);           // Top-left (PI)
    drawCorner(x + w - radius, y + radius, 4.71238898f);       // Top-right (3*PI/2)
}

// -------------------------------------------------------------------------
// Text
// -------------------------------------------------------------------------

void OverlayCanvas::text(const std::string& str, float x, float y, const glm::vec4& color, int fontIndex) {
    textScaled(str, x, y, color, 1.0f, fontIndex);
}

void OverlayCanvas::textScaled(const std::string& str, float x, float y, const glm::vec4& color, float scale, int fontIndex) {
    if (fontIndex < 0 || fontIndex >= 3 || !m_fonts[fontIndex]) return;

    FontProvider& font = *m_fonts[fontIndex];
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

    FontProvider& font = *m_fonts[fontIndex];
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

// -------------------------------------------------------------------------
// Clipping
// -------------------------------------------------------------------------

void OverlayCanvas::beginClipRect(float x, float y, float w, float h) {
    ClipRect rect{x, y, w, h};

    // If there's an existing clip, intersect with it
    if (!m_clipStack.empty()) {
        const auto& prev = m_clipStack.back();
        float x1 = std::max(rect.x, prev.x);
        float y1 = std::max(rect.y, prev.y);
        float x2 = std::min(rect.x + rect.w, prev.x + prev.w);
        float y2 = std::min(rect.y + rect.h, prev.y + prev.h);
        rect.x = x1;
        rect.y = y1;
        rect.w = std::max(0.0f, x2 - x1);
        rect.h = std::max(0.0f, y2 - y1);
    }

    m_clipStack.push_back(rect);
}

void OverlayCanvas::endClipRect() {
    if (!m_clipStack.empty()) {
        m_clipStack.pop_back();
    }
}

glm::vec4 OverlayCanvas::currentClipRect() const {
    if (m_clipStack.empty()) {
        return glm::vec4(0, 0, 0, 0);
    }
    const auto& rect = m_clipStack.back();
    return glm::vec4(rect.x, rect.y, rect.w, rect.h);
}

void OverlayCanvas::setLayerClipRect(int layer, float x, float y, float w, float h) {
    m_layerClipRects[layer] = {x, y, w, h};
}

void OverlayCanvas::clearLayerClipRect(int layer) {
    m_layerClipRects.erase(layer);
}

// -------------------------------------------------------------------------
// Polygon/Polyline
// -------------------------------------------------------------------------

void OverlayCanvas::fillPolygon(const std::vector<glm::vec2>& points, const glm::vec4& color) {
    if (points.size() < 3) return;

    // Transform all points
    std::vector<glm::vec2> transformed;
    transformed.reserve(points.size());
    for (const auto& p : points) {
        transformed.push_back(transformPoint(p));
    }

    // Triangle fan from first vertex
    glm::vec4 premultColor = {color.r * color.a, color.g * color.a, color.b * color.a, color.a};
    for (size_t i = 1; i < transformed.size() - 1; ++i) {
        addQuad(transformed[0], transformed[i], transformed[i + 1], transformed[i + 1], premultColor);
    }
}

void OverlayCanvas::polyline(const std::vector<glm::vec2>& points, float lineWidth, const glm::vec4& color, bool closed) {
    if (points.size() < 2) return;

    size_t count = points.size();
    size_t segments = closed ? count : count - 1;

    for (size_t i = 0; i < segments; ++i) {
        size_t j = (i + 1) % count;
        line(points[i].x, points[i].y, points[j].x, points[j].y, lineWidth, color);
    }
}

} // namespace vivid
