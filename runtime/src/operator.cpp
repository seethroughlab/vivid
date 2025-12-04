// Vivid Operator Implementation

#include "vivid/operator.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "GraphicsTypes.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Texture.h"
#include "TextureView.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "RefCntAutoPtr.hpp"

#include <iostream>

namespace vivid {

using namespace Diligent;

// TextureOperator implementation

TextureOperator::~TextureOperator() {
    cleanup();
}

void TextureOperator::init(Context& ctx) {
    // Create output texture at context resolution
    outputWidth_ = ctx.width();
    outputHeight_ = ctx.height();

    TextureDesc texDesc;
    texDesc.Name = "TextureOperator Output";
    texDesc.Type = RESOURCE_DIM_TEX_2D;
    texDesc.Width = outputWidth_;
    texDesc.Height = outputHeight_;
    texDesc.Format = TEX_FORMAT_RGBA8_UNORM_SRGB;
    texDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    texDesc.Usage = USAGE_DEFAULT;

    RefCntAutoPtr<ITexture> tex;
    ctx.device()->CreateTexture(texDesc, nullptr, &tex);

    if (!tex) {
        std::cerr << "Failed to create output texture for operator" << std::endl;
        return;
    }

    outputTexture_ = tex.Detach();
    outputSRV_ = outputTexture_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    outputRTV_ = outputTexture_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);

    // Let subclass create the pipeline
    createPipeline(ctx);

    // Create SRB if pipeline was created
    if (pso_) {
        pso_->CreateShaderResourceBinding(&srb_, true);
    }
}

void TextureOperator::cleanup() {
    if (srb_) {
        srb_->Release();
        srb_ = nullptr;
    }
    if (uniformBuffer_) {
        uniformBuffer_->Release();
        uniformBuffer_ = nullptr;
    }
    if (pso_) {
        pso_->Release();
        pso_ = nullptr;
    }
    if (outputTexture_) {
        outputTexture_->Release();
        outputTexture_ = nullptr;
    }
    outputSRV_ = nullptr;
    outputRTV_ = nullptr;
}

ITextureView* TextureOperator::getOutputSRV() {
    return outputSRV_;
}

ITextureView* TextureOperator::getOutputRTV() {
    return outputRTV_;
}

void TextureOperator::createUniformBuffer(Context& ctx, size_t size) {
    BufferDesc bufDesc;
    bufDesc.Name = "Operator Uniform Buffer";
    bufDesc.Size = size;
    bufDesc.Usage = USAGE_DYNAMIC;
    bufDesc.BindFlags = BIND_UNIFORM_BUFFER;
    bufDesc.CPUAccessFlags = CPU_ACCESS_WRITE;

    RefCntAutoPtr<IBuffer> buf;
    ctx.device()->CreateBuffer(bufDesc, nullptr, &buf);

    if (buf) {
        uniformBuffer_ = buf.Detach();
    }
}

void TextureOperator::renderFullscreen(Context& ctx) {
    if (!pso_ || !srb_ || !outputRTV_) {
        return;
    }

    auto* immediateCtx = ctx.immediateContext();

    // Set render target to our output texture
    ITextureView* rtvs[] = {outputRTV_};
    immediateCtx->SetRenderTargets(1, rtvs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Clear to transparent black
    float clearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    immediateCtx->ClearRenderTarget(outputRTV_, clearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set viewport
    Viewport vp;
    vp.Width = static_cast<float>(outputWidth_);
    vp.Height = static_cast<float>(outputHeight_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    immediateCtx->SetViewports(1, &vp, outputWidth_, outputHeight_);

    // Update uniforms
    updateUniforms(ctx);

    // Bind input texture if we have one
    ITextureView* inputSRV = getInputSRV(0);
    if (inputSRV && srb_) {
        auto* texVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture");
        if (texVar) {
            texVar->Set(inputSRV);
        }
    }

    // Bind uniform buffer if we have one
    if (uniformBuffer_ && srb_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) {
            cbVar->Set(uniformBuffer_);
        }
    }

    // Set pipeline and draw
    immediateCtx->SetPipelineState(pso_);
    immediateCtx->CommitShaderResources(srb_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Draw fullscreen triangle
    ctx.fullscreenQuad().draw();
}

} // namespace vivid
