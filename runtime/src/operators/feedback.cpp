// Feedback Operator Implementation

#include "vivid/operators/feedback.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "RenderDevice.h"
#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"
#include "Texture.h"
#include "TextureView.h"

namespace vivid {

using namespace Diligent;

static const char* FeedbackPS_Source = R"(
cbuffer Constants : register(b0)
{
    float g_Decay;
    float g_Mix;
    float2 _pad;
};

Texture2D g_Input : register(t0);
Texture2D g_Feedback : register(t1);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(in PSInput input) : SV_TARGET
{
    float4 current = g_Input.Sample(g_Sampler, input.uv);
    float4 feedback = g_Feedback.Sample(g_Sampler, input.uv);

    // Decay the feedback
    feedback *= g_Decay;

    // Mix current with decayed feedback
    return lerp(current, max(current, feedback), g_Mix);
}
)";

void Feedback::createPipeline(Context& ctx) {
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        FeedbackPS_Source,
        "FeedbackPS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) return;

    // Create pipeline with two input textures
    IShader* vs = ctx.shaderUtils().getFullscreenVS();
    if (!vs) {
        ps->Release();
        return;
    }

    GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name = "FeedbackPSO";
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    psoCI.pVS = vs;
    psoCI.pPS = ps;

    psoCI.GraphicsPipeline.InputLayout.NumElements = 0;
    psoCI.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    psoCI.GraphicsPipeline.NumRenderTargets = 1;
    psoCI.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_BGRA8_UNORM_SRGB;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    psoCI.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;

    ShaderResourceVariableDesc vars[] = {
        {SHADER_TYPE_PIXEL, "g_Input", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "g_Feedback", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
    };
    psoCI.PSODesc.ResourceLayout.Variables = vars;
    psoCI.PSODesc.ResourceLayout.NumVariables = 3;

    SamplerDesc samplerDesc;
    samplerDesc.MinFilter = FILTER_TYPE_LINEAR;
    samplerDesc.MagFilter = FILTER_TYPE_LINEAR;
    samplerDesc.AddressU = TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = TEXTURE_ADDRESS_CLAMP;

    ImmutableSamplerDesc immutableSamplers[] = {
        {SHADER_TYPE_PIXEL, "g_Sampler", samplerDesc}
    };
    psoCI.PSODesc.ResourceLayout.ImmutableSamplers = immutableSamplers;
    psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = 1;

    RefCntAutoPtr<IPipelineState> pso;
    ctx.device()->CreateGraphicsPipelineState(psoCI, &pso);
    ps->Release();

    if (!pso) return;

    pso_ = pso.Detach();

    createUniformBuffer(ctx, sizeof(Constants));
    pso_->CreateShaderResourceBinding(&srb_, true);

    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) cbVar->Set(uniformBuffer_);
    }
}

void Feedback::createFeedbackBuffers(Context& ctx) {
    TextureDesc texDesc;
    texDesc.Type = RESOURCE_DIM_TEX_2D;
    texDesc.Width = outputWidth_;
    texDesc.Height = outputHeight_;
    texDesc.Format = TEX_FORMAT_BGRA8_UNORM_SRGB;
    texDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    texDesc.Usage = USAGE_DEFAULT;

    for (int i = 0; i < 2; i++) {
        texDesc.Name = i == 0 ? "FeedbackBuffer0" : "FeedbackBuffer1";

        ctx.device()->CreateTexture(texDesc, nullptr, &feedbackTex_[i]);
        if (feedbackTex_[i]) {
            feedbackRTV_[i] = feedbackTex_[i]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            feedbackSRV_[i] = feedbackTex_[i]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

            // Clear to black
            float clearColor[] = {0, 0, 0, 0};
            ctx.immediateContext()->SetRenderTargets(1, &feedbackRTV_[i], nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            ctx.immediateContext()->ClearRenderTarget(feedbackRTV_[i], clearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
    }

    initialized_ = true;
}

void Feedback::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->decay = decay_;
    cb->mix = mix_;
}

void Feedback::process(Context& ctx) {
    if (!pso_ || !srb_ || !outputRTV_) return;

    // Create feedback buffers on first use or if size changed
    if (!initialized_ || !feedbackTex_[0]) {
        createFeedbackBuffers(ctx);
    }

    auto* immediateCtx = ctx.immediateContext();

    // Set render target to output
    ITextureView* rtvs[] = {outputRTV_};
    immediateCtx->SetRenderTargets(1, rtvs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Viewport vp;
    vp.Width = static_cast<float>(outputWidth_);
    vp.Height = static_cast<float>(outputHeight_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    immediateCtx->SetViewports(1, &vp, outputWidth_, outputHeight_);

    updateUniforms(ctx);

    // Bind input and previous feedback buffer
    ITextureView* inputTex = getInputSRV(0);
    int prevBuffer = 1 - currentBuffer_;

    if (inputTex && srb_) {
        auto* inputVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_Input");
        if (inputVar) inputVar->Set(inputTex);
    }

    if (feedbackSRV_[prevBuffer] && srb_) {
        auto* fbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_Feedback");
        if (fbVar) fbVar->Set(feedbackSRV_[prevBuffer]);
    }

    immediateCtx->SetPipelineState(pso_);
    immediateCtx->CommitShaderResources(srb_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    ctx.fullscreenQuad().draw();

    // Copy output to current feedback buffer for next frame
    CopyTextureAttribs copyAttribs;
    copyAttribs.pSrcTexture = outputTexture_;
    copyAttribs.pDstTexture = feedbackTex_[currentBuffer_];
    immediateCtx->CopyTexture(copyAttribs);

    // Swap buffers
    currentBuffer_ = prevBuffer;
}

void Feedback::cleanup() {
    for (int i = 0; i < 2; i++) {
        if (feedbackTex_[i]) {
            feedbackTex_[i]->Release();
            feedbackTex_[i] = nullptr;
        }
        feedbackRTV_[i] = nullptr;
        feedbackSRV_[i] = nullptr;
    }
    initialized_ = false;

    TextureOperator::cleanup();
}

} // namespace vivid
