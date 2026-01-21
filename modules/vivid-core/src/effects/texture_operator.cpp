// Vivid Effects 2D - TextureOperator Implementation

#include <vivid/effects/texture_operator.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <iostream>
#include <vector>

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
    // If no explicit resolution was set, use window size at init time.
    // This ensures generators match the window rather than the hardcoded default.
    // Resolution is locked after init - window resizes won't affect this operator.
    int width = m_width;
    int height = m_height;
    if (!m_hasExplicitResolution) {
        width = ctx.width();
        height = ctx.height();
    }
    createOutput(ctx, width, height);
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
    // Check if texture size exceeds GPU limits
    uint32_t maxDim = ctx.maxTextureDimension2D();
    if (static_cast<uint32_t>(width) > maxDim || static_cast<uint32_t>(height) > maxDim) {
        std::cerr << "[" << this->name() << "] Texture size " << width << "x" << height
                  << " exceeds GPU limit of " << maxDim << "x" << maxDim << std::endl;
        m_error = "Texture size " + std::to_string(width) + "x" + std::to_string(height)
                + " exceeds GPU limit (" + std::to_string(maxDim) + ")";
        createErrorPlaceholder(ctx);
        return;
    }

    // Clear any previous error
    m_error.clear();

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

void TextureOperator::createErrorPlaceholder(Context& ctx) {
    // Release any existing texture
    releaseOutput();

    // Create a small placeholder texture (64x64 checkerboard)
    constexpr int placeholderSize = 64;
    constexpr int checkSize = 8;  // 8x8 pixel checks
    m_width = placeholderSize;
    m_height = placeholderSize;

    // Create texture (RGBA16Float format)
    WGPUTextureDescriptor desc = {};
    desc.label = toStringView("Error Placeholder");
    desc.size.width = placeholderSize;
    desc.size.height = placeholderSize;
    desc.size.depthOrArrayLayers = 1;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = EFFECTS_FORMAT;
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment |
                 WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;

    m_output.reset(wgpuDeviceCreateTexture(ctx.device(), &desc));

    // Create view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = EFFECTS_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    m_outputView.reset(wgpuTextureCreateView(m_output, &viewDesc));

    // Generate checkerboard pattern in RGBA16Float (8 bytes per pixel)
    // Using half-float: magenta (1,0,1,1) and black (0,0,0,1)
    std::vector<uint16_t> pixels(placeholderSize * placeholderSize * 4);

    // Half-float values: 0x0000 = 0.0, 0x3C00 = 1.0
    constexpr uint16_t zero = 0x0000;
    constexpr uint16_t one = 0x3C00;

    for (int y = 0; y < placeholderSize; ++y) {
        for (int x = 0; x < placeholderSize; ++x) {
            int idx = (y * placeholderSize + x) * 4;
            bool isMagenta = ((x / checkSize) + (y / checkSize)) % 2 == 0;
            if (isMagenta) {
                pixels[idx + 0] = one;   // R
                pixels[idx + 1] = zero;  // G
                pixels[idx + 2] = one;   // B
                pixels[idx + 3] = one;   // A
            } else {
                pixels[idx + 0] = zero;  // R
                pixels[idx + 1] = zero;  // G
                pixels[idx + 2] = zero;  // B
                pixels[idx + 3] = one;   // A
            }
        }
    }

    // Upload to texture
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = m_output;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = placeholderSize * 4 * sizeof(uint16_t);  // 8 bytes per pixel
    layout.rowsPerImage = placeholderSize;

    WGPUExtent3D writeSize = {placeholderSize, placeholderSize, 1};

    wgpuQueueWriteTexture(ctx.queue(), &destination, pixels.data(),
                          pixels.size() * sizeof(uint16_t), &layout, &writeSize);
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
