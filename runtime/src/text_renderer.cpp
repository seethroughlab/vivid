#include "text_renderer.h"
#include "font_atlas.h"
#include "renderer.h"
#include <iostream>
#include <cstring>

namespace vivid {

namespace {

const char* TEXT_SHADER = R"(
// Text rendering shader

struct Uniforms {
    screenSize: vec2f,
    _pad: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(1) @binding(0) var fontTexture: texture_2d<f32>;
@group(1) @binding(1) var fontSampler: sampler;

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
    let pos = (in.position / uniforms.screenSize) * 2.0 - 1.0;
    out.position = vec4f(pos.x, -pos.y, 0.0, 1.0);  // Flip Y for screen coords
    out.uv = in.uv;
    out.color = in.color;

    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let texColor = textureSample(fontTexture, fontSampler, in.uv);
    // Font atlas stores alpha in alpha channel, RGB is white
    return vec4f(in.color.rgb, in.color.a * texColor.a);
}
)";

} // anonymous namespace

TextRenderer::~TextRenderer() {
    if (sampler_) wgpuSamplerRelease(sampler_);
    if (uniformBuffer_) wgpuBufferRelease(uniformBuffer_);
    if (pipeline_) wgpuRenderPipelineRelease(pipeline_);
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
    if (textureLayout_) wgpuBindGroupLayoutRelease(textureLayout_);
    if (uniformLayout_) wgpuBindGroupLayoutRelease(uniformLayout_);
}

bool TextRenderer::init(Renderer& renderer) {
    renderer_ = &renderer;

    if (!createPipeline()) {
        return false;
    }

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    bufferDesc.size = 16;  // vec2 screenSize + padding
    uniformBuffer_ = wgpuDeviceCreateBuffer(renderer_->device(), &bufferDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    sampler_ = wgpuDeviceCreateSampler(renderer_->device(), &samplerDesc);

    std::cout << "[TextRenderer] Initialized\n";
    return true;
}

bool TextRenderer::createPipeline() {
    WGPUDevice device = renderer_->device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = {TEXT_SHADER, strlen(TEXT_SHADER)};

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule) {
        std::cerr << "[TextRenderer] Failed to create shader module\n";
        return false;
    }

    // Create bind group layouts
    // Group 0: Uniforms
    {
        WGPUBindGroupLayoutEntry entry = {};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex;
        entry.buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &entry;
        uniformLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
    }

    // Group 1: Texture + Sampler
    {
        WGPUBindGroupLayoutEntry entries[2] = {};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].texture.sampleType = WGPUTextureSampleType_Float;
        entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 2;
        layoutDesc.entries = entries;
        textureLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
    }

    // Create pipeline layout
    WGPUBindGroupLayout layouts[] = {uniformLayout_, textureLayout_};
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 2;
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Create vertex layout
    WGPUVertexAttribute attributes[3] = {};
    attributes[0].format = WGPUVertexFormat_Float32x2;  // position
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;

    attributes[1].format = WGPUVertexFormat_Float32x2;  // uv
    attributes[1].offset = 8;
    attributes[1].shaderLocation = 1;

    attributes[2].format = WGPUVertexFormat_Float32x4;  // color
    attributes[2].offset = 16;
    attributes[2].shaderLocation = 2;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(TextVertex);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 3;
    vertexLayout.attributes = attributes;

    // Create blend state for alpha blending
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = {.data = "fs_main", .length = 7};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout_;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = {.data = "vs_main", .length = 7};
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    wgpuShaderModuleRelease(shaderModule);

    if (!pipeline_) {
        std::cerr << "[TextRenderer] Failed to create pipeline\n";
        return false;
    }

    return true;
}

void TextRenderer::renderText(FontAtlas& font, const std::string& text,
                               glm::vec2 position, glm::vec4 color,
                               Texture& output, const glm::vec4& clearColor) {
    if (!font.valid() || text.empty()) return;

    vertices_.clear();
    indices_.clear();

    float cursorX = position.x;
    float cursorY = position.y + font.ascent();  // Baseline position

    uint32_t vertexIndex = 0;

    for (char c : text) {
        if (c == '\n') {
            cursorX = position.x;
            cursorY += font.lineHeight();
            continue;
        }

        const GlyphInfo* glyph = font.getGlyph(c);
        if (!glyph) continue;

        // Calculate quad corners
        float x0 = cursorX + glyph->xoff;
        float y0 = cursorY + glyph->yoff;
        float x1 = x0 + glyph->width;
        float y1 = y0 + glyph->height;

        // Add vertices (4 per character)
        vertices_.push_back({{x0, y0}, {glyph->x0, glyph->y0}, color});
        vertices_.push_back({{x1, y0}, {glyph->x1, glyph->y0}, color});
        vertices_.push_back({{x1, y1}, {glyph->x1, glyph->y1}, color});
        vertices_.push_back({{x0, y1}, {glyph->x0, glyph->y1}, color});

        // Add indices (2 triangles per character)
        indices_.push_back(vertexIndex + 0);
        indices_.push_back(vertexIndex + 1);
        indices_.push_back(vertexIndex + 2);
        indices_.push_back(vertexIndex + 0);
        indices_.push_back(vertexIndex + 2);
        indices_.push_back(vertexIndex + 3);

        vertexIndex += 4;
        cursorX += glyph->xadvance;
    }

    if (!vertices_.empty()) {
        renderBatch(font, output, clearColor);
    }
}

void TextRenderer::renderTextAligned(FontAtlas& font, const std::string& text,
                                      glm::vec2 position, TextAlign align, glm::vec4 color,
                                      Texture& output, const glm::vec4& clearColor) {
    glm::vec2 size = font.measureText(text);

    switch (align) {
        case TextAlign::Center:
            position.x -= size.x * 0.5f;
            break;
        case TextAlign::Right:
            position.x -= size.x;
            break;
        default:
            break;
    }

    renderText(font, text, position, color, output, clearColor);
}

void TextRenderer::renderTextCentered(FontAtlas& font, const std::string& text,
                                       glm::vec2 center, glm::vec4 color,
                                       Texture& output, const glm::vec4& clearColor) {
    glm::vec2 size = font.measureText(text);
    glm::vec2 position = center - size * 0.5f;
    renderText(font, text, position, color, output, clearColor);
}

void TextRenderer::renderBatch(FontAtlas& font, Texture& output, const glm::vec4& clearColor) {
    WGPUDevice device = renderer_->device();
    WGPUQueue queue = renderer_->queue();

    // Update uniform buffer
    struct UniformData {
        glm::vec2 screenSize;
        glm::vec2 _pad;
    } uniformData;
    uniformData.screenSize = glm::vec2(output.width, output.height);
    wgpuQueueWriteBuffer(queue, uniformBuffer_, 0, &uniformData, sizeof(uniformData));

    // Create vertex buffer
    WGPUBufferDescriptor vertexBufferDesc = {};
    vertexBufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    vertexBufferDesc.size = vertices_.size() * sizeof(TextVertex);
    WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);
    wgpuQueueWriteBuffer(queue, vertexBuffer, 0, vertices_.data(), vertices_.size() * sizeof(TextVertex));

    // Create index buffer
    WGPUBufferDescriptor indexBufferDesc = {};
    indexBufferDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    indexBufferDesc.size = indices_.size() * sizeof(uint32_t);
    WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(device, &indexBufferDesc);
    wgpuQueueWriteBuffer(queue, indexBuffer, 0, indices_.data(), indices_.size() * sizeof(uint32_t));

    // Create bind groups
    WGPUBindGroup uniformGroup;
    {
        WGPUBindGroupEntry entry = {};
        entry.binding = 0;
        entry.buffer = uniformBuffer_;
        entry.size = 16;

        WGPUBindGroupDescriptor desc = {};
        desc.layout = uniformLayout_;
        desc.entryCount = 1;
        desc.entries = &entry;
        uniformGroup = wgpuDeviceCreateBindGroup(device, &desc);
    }

    WGPUBindGroup textureGroup;
    {
        TextureData* fontTexData = getTextureData(font.texture());

        WGPUBindGroupEntry entries[2] = {};
        entries[0].binding = 0;
        entries[0].textureView = fontTexData->view;
        entries[1].binding = 1;
        entries[1].sampler = sampler_;

        WGPUBindGroupDescriptor desc = {};
        desc.layout = textureLayout_;
        desc.entryCount = 2;
        desc.entries = entries;
        textureGroup = wgpuDeviceCreateBindGroup(device, &desc);
    }

    // Get output texture view
    TextureData* outputData = getTextureData(output);

    // Begin render pass
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

    bool shouldClear = clearColor.a >= 0.0f;

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = outputData->view;
    colorAttachment.loadOp = shouldClear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {clearColor.r, clearColor.g, clearColor.b, glm::max(0.0f, clearColor.a)};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, uniformGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, textureGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, vertices_.size() * sizeof(TextVertex));
    wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32, 0, indices_.size() * sizeof(uint32_t));
    wgpuRenderPassEncoderDrawIndexed(pass, indices_.size(), 1, 0, 0, 0);

    wgpuRenderPassEncoderEnd(pass);

    // Submit
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(queue, 1, &commands);

    // Cleanup
    wgpuCommandBufferRelease(commands);
    wgpuRenderPassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(uniformGroup);
    wgpuBindGroupRelease(textureGroup);
    wgpuBufferRelease(vertexBuffer);
    wgpuBufferRelease(indexBuffer);
}

} // namespace vivid
