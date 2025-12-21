// Vivid Effects 2D - TextureOperator Implementation

#include <vivid/effects/texture_operator.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <iostream>

namespace vivid::effects {

TextureOperator::~TextureOperator() {
    // Handles auto-release via RAII
}

WGPUTextureView TextureOperator::inputView(int index) const {
    Operator* input = getInput(index);

    // If no pointer but we have a name, try to resolve it
    // Note: Resolution requires non-const access, so we defer to process()
    // This const version just returns what we have
    if (!input) return nullptr;

    // Any operator with OutputKind::Texture has an outputView()
    if (input->outputKind() == OutputKind::Texture) {
        return input->outputView();
    }
    return nullptr;
}

void TextureOperator::resolveInputs(Chain& chain) {
    for (size_t i = 0; i < inputNameCount(); ++i) {
        const std::string& name = getInputName(static_cast<int>(i));
        if (name.empty()) continue;

        // Skip if already resolved
        if (getInput(static_cast<int>(i))) continue;

        Operator* op = chain.getByName(name);
        if (op && op->outputKind() == OutputKind::Texture) {
            setInput(static_cast<int>(i), op);
        } else if (!op) {
            std::cerr << "[" << this->name() << "] Input '" << name
                      << "' not found in chain" << std::endl;
        } else {
            std::cerr << "[" << this->name() << "] Input '" << name
                      << "' is not a texture operator" << std::endl;
        }
    }
}

void TextureOperator::createOutput(Context& ctx) {
    createOutput(ctx, m_width, m_height);
}

bool TextureOperator::checkResize(Context& /*ctx*/) {
    // DEPRECATED: No longer auto-resizes to window size.
    // Operators use their declared resolution (default 1280x720).
    // Processors should call matchInputResolution() to inherit input size.
    return false;
}

bool TextureOperator::matchInputResolution(int index) {
    Operator* input = getInput(index);
    if (!input) return false;

    // Get input dimensions
    int inputWidth = 0, inputHeight = 0;
    if (input->outputKind() == OutputKind::Texture) {
        if (auto* texOp = dynamic_cast<TextureOperator*>(input)) {
            inputWidth = texOp->outputWidth();
            inputHeight = texOp->outputHeight();
        }
    }

    if (inputWidth <= 0 || inputHeight <= 0) return false;

    // Resize if dimensions differ
    if (inputWidth != m_width || inputHeight != m_height) {
        m_width = inputWidth;
        m_height = inputHeight;
        markDirty();
        return true;
    }
    return false;
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

    m_output.reset(wgpuDeviceCreateTexture(ctx.device(), &desc));

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = EFFECTS_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    m_outputView.reset(wgpuTextureCreateView(m_output, &viewDesc));
}

void TextureOperator::releaseOutput() {
    m_outputView.reset();
    m_output.reset();
}

void TextureOperator::beginRenderPass(WGPURenderPassEncoder& pass, WGPUCommandEncoder& encoder) {
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
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

    // If using the shared GPU encoder (command buffer batching), don't submit here.
    // The encoder will be submitted by Context::endGpuFrame() after all operators.
    if (ctx.hasActiveGpuEncoder() && encoder == ctx.gpuEncoder()) {
        // Shared encoder - don't finish or submit, just end the pass
        return;
    }

    // Legacy path for operators not yet using shared encoder
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
}

} // namespace vivid::effects
