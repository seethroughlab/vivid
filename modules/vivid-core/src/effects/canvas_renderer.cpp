#include "canvas_renderer.h"
#include "font_atlas.h"
#include <vivid/asset_loader.h>
#include <vivid/context.h>
#include <vivid/effects/texture_operator.h>
#include <cmath>
#include <iostream>

namespace vivid {

// Shader source loaded from external file
static std::string s_canvasShader;

static void ensureCanvasShadersLoaded() {
    if (s_canvasShader.empty()) {
        s_canvasShader = AssetLoader::instance().loadShader("canvas.wgsl");
    }
}

CanvasRenderer::~CanvasRenderer() {
    cleanup();
}

void CanvasRenderer::cleanup() {
    if (m_fontBindGroup) {
        wgpuBindGroupRelease(m_fontBindGroup);
        m_fontBindGroup = nullptr;
    }
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
    // Clean up clip pipeline
    if (m_clipPipeline) {
        wgpuRenderPipelineRelease(m_clipPipeline);
        m_clipPipeline = nullptr;
    }
    // Clean up image pipeline
    if (m_imagePipeline) {
        wgpuRenderPipelineRelease(m_imagePipeline);
        m_imagePipeline = nullptr;
    }
    // Clean up stencil buffer
    if (m_stencilView) {
        wgpuTextureViewRelease(m_stencilView);
        m_stencilView = nullptr;
    }
    if (m_stencilTexture) {
        wgpuTextureRelease(m_stencilTexture);
        m_stencilTexture = nullptr;
    }
    m_stencilWidth = 0;
    m_stencilHeight = 0;
    // Clean up persistent vertex/index buffers
    if (m_vertexBuffer) {
        wgpuBufferRelease(m_vertexBuffer);
        m_vertexBuffer = nullptr;
    }
    if (m_indexBuffer) {
        wgpuBufferRelease(m_indexBuffer);
        m_indexBuffer = nullptr;
    }
    m_vertexCapacity = 0;
    m_indexCapacity = 0;
    m_initialized = false;
}

bool CanvasRenderer::init(Context& ctx) {
    if (m_initialized) return true;

    createPipeline(ctx);
    createWhiteTexture(ctx);

    m_initialized = true;
    return true;
}

void CanvasRenderer::createPipeline(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Ensure shaders are loaded
    ensureCanvasShadersLoaded();

    // Create shader module (use external shader, fallback to embedded)
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    const std::string& shaderSource = s_canvasShader;
    wgslDesc.code = effects::toStringView(shaderSource.c_str());

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Bind group layout
    WGPUBindGroupLayoutEntry entries[3] = {};

    // Uniforms
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = 16;  // vec2 + vec2 padding

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
    vertexLayout.arrayStride = sizeof(CanvasVertex);
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
    colorTarget.format = effects::EFFECTS_FORMAT;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = effects::toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Depth/stencil state for main pipeline (test against stencil)
    // WebGPU comparison is: reference COMPARE stencil_value
    // LessEqual means: pass when reference <= stencil_value
    // When clipDepth = 0 (no clip), reference=0, always passes (0 <= anything)
    // When clipDepth = 1, only pixels where stencil was written (= 1) pass (1 <= 1)
    WGPUDepthStencilState depthStencilState = {};
    depthStencilState.format = WGPUTextureFormat_Stencil8;
    depthStencilState.stencilFront.compare = WGPUCompareFunction_LessEqual;
    depthStencilState.stencilFront.failOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilFront.passOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilBack = depthStencilState.stencilFront;
    depthStencilState.stencilReadMask = 0xFF;
    depthStencilState.stencilWriteMask = 0x00;  // Don't write stencil during normal draw

    // Render pipeline (main drawing with stencil test)
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = effects::toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.depthStencil = &depthStencilState;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // Create image pipeline (uses fs_image shader for full RGBA texture sampling)
    WGPUFragmentState imageFragmentState = fragmentState;
    imageFragmentState.entryPoint = effects::toStringView("fs_image");
    WGPURenderPipelineDescriptor imagePipelineDesc = pipelineDesc;
    imagePipelineDesc.fragment = &imageFragmentState;
    m_imagePipeline = wgpuDeviceCreateRenderPipeline(device, &imagePipelineDesc);

    // Create clip pipeline (writes to stencil, no color output)
    WGPUDepthStencilState clipDepthStencilState = {};
    clipDepthStencilState.format = WGPUTextureFormat_Stencil8;
    clipDepthStencilState.stencilFront.compare = WGPUCompareFunction_Always;
    clipDepthStencilState.stencilFront.failOp = WGPUStencilOperation_Keep;
    clipDepthStencilState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    clipDepthStencilState.stencilFront.passOp = WGPUStencilOperation_Replace;  // Write stencil ref
    clipDepthStencilState.stencilBack = clipDepthStencilState.stencilFront;
    clipDepthStencilState.stencilReadMask = 0xFF;
    clipDepthStencilState.stencilWriteMask = 0xFF;

    // Clip pipeline doesn't write color, only stencil
    WGPUColorTargetState clipColorTarget = colorTarget;
    clipColorTarget.writeMask = WGPUColorWriteMask_None;

    WGPUFragmentState clipFragmentState = fragmentState;
    clipFragmentState.targets = &clipColorTarget;

    WGPURenderPipelineDescriptor clipPipelineDesc = pipelineDesc;
    clipPipelineDesc.depthStencil = &clipDepthStencilState;
    clipPipelineDesc.fragment = &clipFragmentState;

    m_clipPipeline = wgpuDeviceCreateRenderPipeline(device, &clipPipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = 16;  // vec2 resolution + vec2 padding
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

void CanvasRenderer::createWhiteTexture(Context& ctx) {
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

void CanvasRenderer::createStencilTexture(Context& ctx, int width, int height) {
    // Skip if already the right size
    if (m_stencilTexture && m_stencilWidth == width && m_stencilHeight == height) {
        return;
    }

    // Release old stencil resources
    if (m_stencilView) {
        wgpuTextureViewRelease(m_stencilView);
        m_stencilView = nullptr;
    }
    if (m_stencilTexture) {
        wgpuTextureRelease(m_stencilTexture);
        m_stencilTexture = nullptr;
    }

    WGPUDevice device = ctx.device();

    // Create stencil texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.usage = WGPUTextureUsage_RenderAttachment;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    texDesc.format = WGPUTextureFormat_Stencil8;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    m_stencilTexture = wgpuDeviceCreateTexture(device, &texDesc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_Stencil8;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_StencilOnly;
    m_stencilView = wgpuTextureCreateView(m_stencilTexture, &viewDesc);

    m_stencilWidth = width;
    m_stencilHeight = height;
}

void CanvasRenderer::begin(int width, int height, const glm::vec4& clearColor) {
    m_drawCommands.clear();
    m_currentVertices.clear();
    m_currentIndices.clear();
    m_currentBatchType = DrawCmdType::Solid;
    m_currentTextureView = nullptr;
    m_clipCommands.clear();
    m_clipDepth = 0;
    m_width = width;
    m_height = height;
    m_clearColor = clearColor;
    m_currentFont = nullptr;
}

void CanvasRenderer::flushCurrentBatch() {
    if (m_currentVertices.empty()) return;

    DrawCmd cmd;
    cmd.type = m_currentBatchType;
    cmd.vertices = std::move(m_currentVertices);
    cmd.indices = std::move(m_currentIndices);
    cmd.textureView = m_currentTextureView;
    cmd.clipDepth = m_clipDepth;

    m_drawCommands.push_back(std::move(cmd));

    m_currentVertices.clear();
    m_currentIndices.clear();
    m_currentTextureView = nullptr;
}

void CanvasRenderer::addSolidQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, const glm::vec4& color) {
    // Flush if switching from a different batch type
    if (m_currentBatchType != DrawCmdType::Solid) {
        flushCurrentBatch();
        m_currentBatchType = DrawCmdType::Solid;
    }

    uint32_t baseIndex = static_cast<uint32_t>(m_currentVertices.size());
    glm::vec2 uv(0.5f, 0.5f);  // Center of white texture

    m_currentVertices.push_back({p0, uv, color});
    m_currentVertices.push_back({p1, uv, color});
    m_currentVertices.push_back({p2, uv, color});
    m_currentVertices.push_back({p3, uv, color});

    // Two triangles: 0-1-2, 0-2-3
    m_currentIndices.push_back(baseIndex + 0);
    m_currentIndices.push_back(baseIndex + 1);
    m_currentIndices.push_back(baseIndex + 2);
    m_currentIndices.push_back(baseIndex + 0);
    m_currentIndices.push_back(baseIndex + 2);
    m_currentIndices.push_back(baseIndex + 3);
}

void CanvasRenderer::addTextQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                                  glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3,
                                  const glm::vec4& color) {
    // Flush if switching from a different batch type
    if (m_currentBatchType != DrawCmdType::Text) {
        flushCurrentBatch();
        m_currentBatchType = DrawCmdType::Text;
    }

    uint32_t baseIndex = static_cast<uint32_t>(m_currentVertices.size());

    m_currentVertices.push_back({p0, uv0, color});
    m_currentVertices.push_back({p1, uv1, color});
    m_currentVertices.push_back({p2, uv2, color});
    m_currentVertices.push_back({p3, uv3, color});

    // Two triangles: 0-1-2, 0-2-3
    m_currentIndices.push_back(baseIndex + 0);
    m_currentIndices.push_back(baseIndex + 1);
    m_currentIndices.push_back(baseIndex + 2);
    m_currentIndices.push_back(baseIndex + 0);
    m_currentIndices.push_back(baseIndex + 2);
    m_currentIndices.push_back(baseIndex + 3);
}

void CanvasRenderer::addImage(WGPUTextureView textureView,
                              int srcWidth, int srcHeight,
                              float sx, float sy, float sw, float sh,
                              float dx, float dy, float dw, float dh,
                              float alpha) {
    if (!textureView) return;

    // Flush if switching from different type OR different texture view
    // (Images with different textures cannot be batched together)
    if (m_currentBatchType != DrawCmdType::Image || m_currentTextureView != textureView) {
        flushCurrentBatch();
        m_currentBatchType = DrawCmdType::Image;
        m_currentTextureView = textureView;
    }

    // Calculate UVs based on source rect
    float u0 = sx / srcWidth;
    float v0 = sy / srcHeight;
    float u1 = (sx + sw) / srcWidth;
    float v1 = (sy + sh) / srcHeight;

    // Create quad vertices with UV coords
    glm::vec4 color(1.0f, 1.0f, 1.0f, alpha);

    uint32_t baseIndex = static_cast<uint32_t>(m_currentVertices.size());

    // Four corners
    m_currentVertices.push_back({{dx, dy}, {u0, v0}, color});
    m_currentVertices.push_back({{dx + dw, dy}, {u1, v0}, color});
    m_currentVertices.push_back({{dx + dw, dy + dh}, {u1, v1}, color});
    m_currentVertices.push_back({{dx, dy + dh}, {u0, v1}, color});

    // Two triangles
    m_currentIndices.push_back(baseIndex + 0);
    m_currentIndices.push_back(baseIndex + 1);
    m_currentIndices.push_back(baseIndex + 2);
    m_currentIndices.push_back(baseIndex + 0);
    m_currentIndices.push_back(baseIndex + 2);
    m_currentIndices.push_back(baseIndex + 3);
}

void CanvasRenderer::rectFilled(float x, float y, float w, float h, const glm::vec4& color) {
    addSolidQuad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, color);
}

void CanvasRenderer::rect(float x, float y, float w, float h, float lineWidth, const glm::vec4& color) {
    // Draw as 4 thin rectangles
    rectFilled(x, y, w, lineWidth, color);           // Top
    rectFilled(x, y + h - lineWidth, w, lineWidth, color);  // Bottom
    rectFilled(x, y + lineWidth, lineWidth, h - 2 * lineWidth, color);  // Left
    rectFilled(x + w - lineWidth, y + lineWidth, lineWidth, h - 2 * lineWidth, color);  // Right
}

void CanvasRenderer::circleFilled(float cx, float cy, float radius, const glm::vec4& color, int segments) {
    // Flush if switching from a different batch type
    if (m_currentBatchType != DrawCmdType::Solid) {
        flushCurrentBatch();
        m_currentBatchType = DrawCmdType::Solid;
    }

    glm::vec2 uv(0.5f, 0.5f);
    uint32_t centerIndex = static_cast<uint32_t>(m_currentVertices.size());

    // Center vertex
    m_currentVertices.push_back({{cx, cy}, uv, color});

    // Edge vertices
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159265f;
        float px = cx + std::cos(angle) * radius;
        float py = cy + std::sin(angle) * radius;
        m_currentVertices.push_back({{px, py}, uv, color});
    }

    // Triangles (fan)
    for (int i = 0; i < segments; i++) {
        m_currentIndices.push_back(centerIndex);
        m_currentIndices.push_back(centerIndex + 1 + i);
        m_currentIndices.push_back(centerIndex + 2 + i);
    }
}

void CanvasRenderer::circle(float cx, float cy, float radius, float lineWidth, const glm::vec4& color, int segments) {
    float innerRadius = radius - lineWidth;

    for (int i = 0; i < segments; i++) {
        float angle0 = (float)i / segments * 2.0f * 3.14159265f;
        float angle1 = (float)(i + 1) / segments * 2.0f * 3.14159265f;

        float cos0 = std::cos(angle0), sin0 = std::sin(angle0);
        float cos1 = std::cos(angle1), sin1 = std::sin(angle1);

        glm::vec2 outer0 = {cx + cos0 * radius, cy + sin0 * radius};
        glm::vec2 outer1 = {cx + cos1 * radius, cy + sin1 * radius};
        glm::vec2 inner0 = {cx + cos0 * innerRadius, cy + sin0 * innerRadius};
        glm::vec2 inner1 = {cx + cos1 * innerRadius, cy + sin1 * innerRadius};

        addSolidQuad(outer0, outer1, inner1, inner0, color);
    }
}

void CanvasRenderer::line(float x1, float y1, float x2, float y2, float width, const glm::vec4& color) {
    glm::vec2 dir = glm::normalize(glm::vec2(x2 - x1, y2 - y1));
    glm::vec2 perp(-dir.y, dir.x);
    float halfWidth = width * 0.5f;

    glm::vec2 p0 = glm::vec2(x1, y1) - perp * halfWidth;
    glm::vec2 p1 = glm::vec2(x1, y1) + perp * halfWidth;
    glm::vec2 p2 = glm::vec2(x2, y2) + perp * halfWidth;
    glm::vec2 p3 = glm::vec2(x2, y2) - perp * halfWidth;

    addSolidQuad(p0, p1, p2, p3, color);
}

void CanvasRenderer::triangleFilled(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& color) {
    // Flush if switching from a different batch type
    if (m_currentBatchType != DrawCmdType::Solid) {
        flushCurrentBatch();
        m_currentBatchType = DrawCmdType::Solid;
    }

    glm::vec2 uv(0.5f, 0.5f);
    uint32_t baseIndex = static_cast<uint32_t>(m_currentVertices.size());

    m_currentVertices.push_back({a, uv, color});
    m_currentVertices.push_back({b, uv, color});
    m_currentVertices.push_back({c, uv, color});

    m_currentIndices.push_back(baseIndex + 0);
    m_currentIndices.push_back(baseIndex + 1);
    m_currentIndices.push_back(baseIndex + 2);
}

void CanvasRenderer::addClip(const std::vector<glm::vec2>& vertices, const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) return;

    // Flush any pending geometry before changing clip state
    flushCurrentBatch();

    ClipCmd cmd;
    cmd.clipDepth = m_clipDepth;

    // Convert vec2 vertices to CanvasVertex format
    glm::vec2 uv(0.5f, 0.5f);
    glm::vec4 color(1.0f);  // Color doesn't matter for stencil writes (no color output)

    for (const auto& v : vertices) {
        cmd.vertices.push_back({v, uv, color});
    }
    cmd.indices = indices;

    m_clipCommands.push_back(std::move(cmd));
}

void CanvasRenderer::setClipDepth(int depth) {
    if (depth != m_clipDepth) {
        // Flush any pending geometry before changing clip state
        flushCurrentBatch();
        m_clipDepth = depth;
    }
}

void CanvasRenderer::text(FontAtlas& font, const std::string& str, float x, float y,
                          const glm::vec4& color, float letterSpacing) {
    m_currentFont = &font;

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

        // Apply kerning from previous character
        if (prevChar != 0) {
            cursorX += font.getKerning(prevChar, c);
        }

        float x0 = cursorX + glyph->xoff;
        float y0 = cursorY + glyph->yoff;
        float x1 = x0 + glyph->width;
        float y1 = y0 + glyph->height;

        addTextQuad(
            {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1},
            {glyph->u0, glyph->v0}, {glyph->u1, glyph->v0},
            {glyph->u1, glyph->v1}, {glyph->u0, glyph->v1},
            color
        );

        cursorX += glyph->xadvance + letterSpacing;
        prevChar = c;
    }
}

void CanvasRenderer::renderBatch(WGPURenderPassEncoder pass, Context& ctx,
                                   const std::vector<CanvasVertex>& vertices,
                                   const std::vector<uint32_t>& indices,
                                   WGPUBindGroup bindGroup) {
    // This version is for the persistent buffers managed by render()
    // The actual buffer management is done in render() - this just draws
    (void)ctx;  // Unused in this version
    if (vertices.empty()) return;

    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDrawIndexed(pass, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
}

void CanvasRenderer::render(Context& ctx, WGPUTexture targetTexture, WGPUTextureView targetView) {
    // Flush any pending vertices to a command
    flushCurrentBatch();

    // Check if any commands exist
    if (m_drawCommands.empty()) return;

    WGPUDevice device = ctx.device();
    WGPUQueue queue = ctx.queue();

    // Always create stencil texture (pipeline requires it)
    createStencilTexture(ctx, m_width, m_height);
    bool useStencil = !m_clipCommands.empty();

    // Update uniforms
    float uniforms[4] = {(float)m_width, (float)m_height, 0.0f, 0.0f};
    wgpuQueueWriteBuffer(queue, m_uniformBuffer, 0, uniforms, sizeof(uniforms));

    // Create font bind group if text commands exist
    bool hasTextCommands = false;
    for (const auto& cmd : m_drawCommands) {
        if (cmd.type == DrawCmdType::Text) {
            hasTextCommands = true;
            break;
        }
    }
    if (m_currentFont && m_currentFont->textureView() && hasTextCommands) {
        WGPUBindGroupEntry bgEntries[3] = {};
        bgEntries[0].binding = 0;
        bgEntries[0].buffer = m_uniformBuffer;
        bgEntries[0].size = 16;
        bgEntries[1].binding = 1;
        bgEntries[1].sampler = m_sampler;
        bgEntries[2].binding = 2;
        bgEntries[2].textureView = m_currentFont->textureView();

        WGPUBindGroupDescriptor bgDesc = {};
        bgDesc.layout = m_bindGroupLayout;
        bgDesc.entryCount = 3;
        bgDesc.entries = bgEntries;

        if (m_fontBindGroup) {
            wgpuBindGroupRelease(m_fontBindGroup);
        }
        m_fontBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
    }

    // Use shared command encoder for batched submission
    WGPUCommandEncoder encoder = ctx.gpuEncoder();

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a};

    // Always setup stencil attachment (pipeline requires it)
    WGPURenderPassDepthStencilAttachment stencilAttachment = {};
    stencilAttachment.view = m_stencilView;
    stencilAttachment.stencilLoadOp = WGPULoadOp_Clear;
    stencilAttachment.stencilStoreOp = WGPUStoreOp_Store;
    stencilAttachment.stencilClearValue = 0;
    stencilAttachment.stencilReadOnly = false;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &stencilAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    // Process clip commands first (write to stencil)
    if (!m_clipCommands.empty()) {
        wgpuRenderPassEncoderSetPipeline(pass, m_clipPipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_whiteBindGroup, 0, nullptr);

        for (const auto& clipCmd : m_clipCommands) {
            if (clipCmd.vertices.empty()) continue;

            // Create temporary buffers for this clip command
            WGPUBufferDescriptor vbDesc = {};
            vbDesc.size = clipCmd.vertices.size() * sizeof(CanvasVertex);
            vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            WGPUBuffer vb = wgpuDeviceCreateBuffer(device, &vbDesc);
            wgpuQueueWriteBuffer(queue, vb, 0, clipCmd.vertices.data(), vbDesc.size);

            WGPUBufferDescriptor ibDesc = {};
            ibDesc.size = clipCmd.indices.size() * sizeof(uint32_t);
            ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            WGPUBuffer ib = wgpuDeviceCreateBuffer(device, &ibDesc);
            wgpuQueueWriteBuffer(queue, ib, 0, clipCmd.indices.data(), ibDesc.size);

            wgpuRenderPassEncoderSetStencilReference(pass, static_cast<uint32_t>(clipCmd.clipDepth));
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, vbDesc.size);
            wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, ibDesc.size);
            wgpuRenderPassEncoderDrawIndexed(pass, static_cast<uint32_t>(clipCmd.indices.size()), 1, 0, 0, 0);

            wgpuBufferRelease(vb);
            wgpuBufferRelease(ib);
        }
    }

    // Process draw commands in order (painter's algorithm)
    // Track current pipeline to minimize state changes
    DrawCmdType currentPipeline = DrawCmdType::Solid;
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_whiteBindGroup, 0, nullptr);

    for (const auto& cmd : m_drawCommands) {
        if (cmd.vertices.empty()) continue;

        // Switch pipeline if needed
        if (cmd.type != currentPipeline) {
            currentPipeline = cmd.type;
            switch (cmd.type) {
                case DrawCmdType::Solid:
                    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
                    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_whiteBindGroup, 0, nullptr);
                    break;
                case DrawCmdType::Text:
                    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
                    if (m_fontBindGroup) {
                        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_fontBindGroup, 0, nullptr);
                    }
                    break;
                case DrawCmdType::Image:
                    wgpuRenderPassEncoderSetPipeline(pass, m_imagePipeline);
                    break;
            }
        }

        // Set stencil reference
        if (useStencil) {
            wgpuRenderPassEncoderSetStencilReference(pass, static_cast<uint32_t>(cmd.clipDepth));
        }

        // Create temporary buffers
        WGPUBufferDescriptor vbDesc = {};
        vbDesc.size = cmd.vertices.size() * sizeof(CanvasVertex);
        vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        WGPUBuffer vb = wgpuDeviceCreateBuffer(device, &vbDesc);
        wgpuQueueWriteBuffer(queue, vb, 0, cmd.vertices.data(), vbDesc.size);

        WGPUBufferDescriptor ibDesc = {};
        ibDesc.size = cmd.indices.size() * sizeof(uint32_t);
        ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
        WGPUBuffer ib = wgpuDeviceCreateBuffer(device, &ibDesc);
        wgpuQueueWriteBuffer(queue, ib, 0, cmd.indices.data(), ibDesc.size);

        // For images, create a bind group for the texture
        WGPUBindGroup imageBindGroup = nullptr;
        if (cmd.type == DrawCmdType::Image && cmd.textureView) {
            WGPUBindGroupEntry bgEntries[3] = {};
            bgEntries[0].binding = 0;
            bgEntries[0].buffer = m_uniformBuffer;
            bgEntries[0].size = 16;
            bgEntries[1].binding = 1;
            bgEntries[1].sampler = m_sampler;
            bgEntries[2].binding = 2;
            bgEntries[2].textureView = cmd.textureView;

            WGPUBindGroupDescriptor bgDesc = {};
            bgDesc.layout = m_bindGroupLayout;
            bgDesc.entryCount = 3;
            bgDesc.entries = bgEntries;
            imageBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, imageBindGroup, 0, nullptr);
        }

        // Draw
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, vbDesc.size);
        wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, ibDesc.size);
        wgpuRenderPassEncoderDrawIndexed(pass, static_cast<uint32_t>(cmd.indices.size()), 1, 0, 0, 0);

        // Release temporary resources
        if (imageBindGroup) {
            wgpuBindGroupRelease(imageBindGroup);
        }
        wgpuBufferRelease(vb);
        wgpuBufferRelease(ib);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Don't submit - using shared encoder from Context
    // The encoder will be submitted by Chain::process() via ctx.endGpuFrame()
}

} // namespace vivid
