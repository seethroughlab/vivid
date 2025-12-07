// Vivid Effects 2D - TextureOperator Implementation

#include <vivid/effects/texture_operator.h>
#include <vivid/context.h>

namespace vivid::effects {

TextureOperator::~TextureOperator() {
    releaseOutput();
}

WGPUTextureView TextureOperator::inputView(int index) const {
    Operator* input = getInput(index);
    if (!input) return nullptr;

    // Any operator with OutputKind::Texture has an outputView()
    if (input->outputKind() == OutputKind::Texture) {
        return input->outputView();
    }
    return nullptr;
}

void TextureOperator::createOutput(Context& ctx) {
    createOutput(ctx, m_width, m_height);
}

void TextureOperator::createOutput(Context& ctx, int width, int height) {
    // Release existing if dimensions changed
    if (m_output && (m_width != width || m_height != height)) {
        releaseOutput();
    }

    m_width = width;
    m_height = height;

    if (m_output) return; // Already created with same dimensions

    WGPUTextureDescriptor desc = {};
    desc.label = toStringView("TextureOperator Output");
    desc.size.width = static_cast<uint32_t>(width);
    desc.size.height = static_cast<uint32_t>(height);
    desc.size.depthOrArrayLayers = 1;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = EFFECTS_FORMAT;
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;

    m_output = wgpuDeviceCreateTexture(ctx.device(), &desc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = EFFECTS_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    m_outputView = wgpuTextureCreateView(m_output, &viewDesc);
}

void TextureOperator::releaseOutput() {
    if (m_outputView) {
        wgpuTextureViewRelease(m_outputView);
        m_outputView = nullptr;
    }
    if (m_output) {
        wgpuTextureRelease(m_output);
        m_output = nullptr;
    }
}

void TextureOperator::beginRenderPass(WGPURenderPassEncoder& pass, WGPUCommandEncoder& encoder) {
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
}

void TextureOperator::endRenderPass(WGPURenderPassEncoder pass, WGPUCommandEncoder encoder, Context& ctx) {
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
}

} // namespace vivid::effects
