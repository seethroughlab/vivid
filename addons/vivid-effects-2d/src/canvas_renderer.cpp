#include "canvas_renderer.h"
#include "font_atlas.h"
#include <vivid/context.h>
#include <vivid/effects/texture_operator.h>
#include <cmath>
#include <iostream>

namespace vivid {

// Embedded shader for canvas rendering
static const char* CANVAS_SHADER = R"(
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
    // Use texture alpha * vertex color for text rendering
    // For solid shapes, texture is white (1,1,1,1)
    return vec4f(in.color.rgb, in.color.a * texColor.a);
}
)";

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

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = effects::toStringView(CANVAS_SHADER);

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

    // Render pipeline
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
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

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

void CanvasRenderer::begin(int width, int height, const glm::vec4& clearColor) {
    m_solidVertices.clear();
    m_solidIndices.clear();
    m_textVertices.clear();
    m_textIndices.clear();
    m_width = width;
    m_height = height;
    m_clearColor = clearColor;
    m_currentFont = nullptr;
}

void CanvasRenderer::addSolidQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, const glm::vec4& color) {
    uint32_t baseIndex = static_cast<uint32_t>(m_solidVertices.size());
    glm::vec2 uv(0.5f, 0.5f);  // Center of white texture

    m_solidVertices.push_back({p0, uv, color});
    m_solidVertices.push_back({p1, uv, color});
    m_solidVertices.push_back({p2, uv, color});
    m_solidVertices.push_back({p3, uv, color});

    // Two triangles: 0-1-2, 0-2-3
    m_solidIndices.push_back(baseIndex + 0);
    m_solidIndices.push_back(baseIndex + 1);
    m_solidIndices.push_back(baseIndex + 2);
    m_solidIndices.push_back(baseIndex + 0);
    m_solidIndices.push_back(baseIndex + 2);
    m_solidIndices.push_back(baseIndex + 3);
}

void CanvasRenderer::addTextQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                                  glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3,
                                  const glm::vec4& color) {
    uint32_t baseIndex = static_cast<uint32_t>(m_textVertices.size());

    m_textVertices.push_back({p0, uv0, color});
    m_textVertices.push_back({p1, uv1, color});
    m_textVertices.push_back({p2, uv2, color});
    m_textVertices.push_back({p3, uv3, color});

    // Two triangles: 0-1-2, 0-2-3
    m_textIndices.push_back(baseIndex + 0);
    m_textIndices.push_back(baseIndex + 1);
    m_textIndices.push_back(baseIndex + 2);
    m_textIndices.push_back(baseIndex + 0);
    m_textIndices.push_back(baseIndex + 2);
    m_textIndices.push_back(baseIndex + 3);
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
    glm::vec2 uv(0.5f, 0.5f);
    uint32_t centerIndex = static_cast<uint32_t>(m_solidVertices.size());

    // Center vertex
    m_solidVertices.push_back({{cx, cy}, uv, color});

    // Edge vertices
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159265f;
        float px = cx + std::cos(angle) * radius;
        float py = cy + std::sin(angle) * radius;
        m_solidVertices.push_back({{px, py}, uv, color});
    }

    // Triangles (fan)
    for (int i = 0; i < segments; i++) {
        m_solidIndices.push_back(centerIndex);
        m_solidIndices.push_back(centerIndex + 1 + i);
        m_solidIndices.push_back(centerIndex + 2 + i);
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
    glm::vec2 uv(0.5f, 0.5f);
    uint32_t baseIndex = static_cast<uint32_t>(m_solidVertices.size());

    m_solidVertices.push_back({a, uv, color});
    m_solidVertices.push_back({b, uv, color});
    m_solidVertices.push_back({c, uv, color});

    m_solidIndices.push_back(baseIndex + 0);
    m_solidIndices.push_back(baseIndex + 1);
    m_solidIndices.push_back(baseIndex + 2);
}

void CanvasRenderer::text(FontAtlas& font, const std::string& str, float x, float y, const glm::vec4& color) {
    m_currentFont = &font;

    float cursorX = x;
    float cursorY = y;

    for (char c : str) {
        if (c == '\n') {
            cursorX = x;
            cursorY += font.lineHeight();
            continue;
        }

        const GlyphInfo* glyph = font.getGlyph(c);
        if (!glyph) continue;

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

        cursorX += glyph->xadvance;
    }
}

void CanvasRenderer::renderBatch(WGPURenderPassEncoder pass, Context& ctx,
                                   const std::vector<CanvasVertex>& vertices,
                                   const std::vector<uint32_t>& indices,
                                   WGPUBindGroup bindGroup) {
    if (vertices.empty()) return;

    WGPUDevice device = ctx.device();
    WGPUQueue queue = ctx.queue();

    // Create vertex buffer
    WGPUBufferDescriptor vbDesc = {};
    vbDesc.size = vertices.size() * sizeof(CanvasVertex);
    vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device, &vbDesc);
    wgpuQueueWriteBuffer(queue, vertexBuffer, 0, vertices.data(), vbDesc.size);

    // Create index buffer
    WGPUBufferDescriptor ibDesc = {};
    ibDesc.size = indices.size() * sizeof(uint32_t);
    ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(device, &ibDesc);
    wgpuQueueWriteBuffer(queue, indexBuffer, 0, indices.data(), ibDesc.size);

    // Render the batch
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, vbDesc.size);
    wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32, 0, ibDesc.size);
    wgpuRenderPassEncoderDrawIndexed(pass, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    // Release buffers
    wgpuBufferRelease(vertexBuffer);
    wgpuBufferRelease(indexBuffer);
}

void CanvasRenderer::render(Context& ctx, WGPUTexture targetTexture, WGPUTextureView targetView) {
    // Check if either batch has content
    if (m_solidVertices.empty() && m_textVertices.empty()) return;

    WGPUDevice device = ctx.device();
    WGPUQueue queue = ctx.queue();

    // Update uniforms
    float uniforms[4] = {(float)m_width, (float)m_height, 0.0f, 0.0f};
    wgpuQueueWriteBuffer(queue, m_uniformBuffer, 0, uniforms, sizeof(uniforms));

    // Create font bind group if text was used
    if (m_currentFont && m_currentFont->textureView() && !m_textVertices.empty()) {
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

        // Debug: Print text rendering info for livery canvas (1024x1024)
        static int debugCount = 0;
        if (debugCount < 2 && m_width == 1024) {
            std::cout << "[CanvasRenderer] Livery text: canvas=" << m_width << "x" << m_height
                      << " verts=" << m_textVertices.size() << std::endl;
            // Print all 4 vertices of first glyph
            for (int i = 0; i < std::min(4, (int)m_textVertices.size()); i++) {
                auto& v = m_textVertices[i];
                std::cout << "  v" << i << ": pos=(" << v.position.x << "," << v.position.y << ")"
                          << " uv=(" << v.uv.x << "," << v.uv.y << ")"
                          << " color.a=" << v.color.a << std::endl;
            }
            debugCount++;
        }
    }

    // Begin render pass
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);

    // Render solid primitives first (with white texture)
    renderBatch(pass, ctx, m_solidVertices, m_solidIndices, m_whiteBindGroup);

    // Render text primitives second (with font texture)
    if (m_fontBindGroup && !m_textVertices.empty()) {
        renderBatch(pass, ctx, m_textVertices, m_textIndices, m_fontBindGroup);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
}

} // namespace vivid
