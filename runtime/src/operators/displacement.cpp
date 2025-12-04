// Displacement Operator Implementation

#include "vivid/operators/displacement.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "RenderDevice.h"
#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

namespace vivid {

using namespace Diligent;

static const char* DisplacementPS_Source = R"(
cbuffer Constants : register(b0)
{
    float g_Amount;
    float g_ScaleX;
    float g_ScaleY;
    float _pad;
};

Texture2D g_Source : register(t0);
Texture2D g_DisplacementMap : register(t1);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(in PSInput input) : SV_TARGET
{
    // Sample displacement map (red = x, green = y)
    float4 disp = g_DisplacementMap.Sample(g_Sampler, input.uv);

    // Convert from [0,1] to [-1,1]
    float2 offset;
    offset.x = (disp.r - 0.5) * 2.0 * g_Amount * g_ScaleX;
    offset.y = (disp.g - 0.5) * 2.0 * g_Amount * g_ScaleY;

    // Apply displacement
    float2 displacedUV = input.uv + offset;

    return g_Source.Sample(g_Sampler, displacedUV);
}
)";

void Displacement::createPipeline(Context& ctx) {
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        DisplacementPS_Source,
        "DisplacementPS",
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
    psoCI.PSODesc.Name = "DisplacementPSO";
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
        {SHADER_TYPE_PIXEL, "g_Source", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "g_DisplacementMap", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
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

void Displacement::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->amount = amount_;
    cb->scaleX = scaleX_;
    cb->scaleY = scaleY_;
}

void Displacement::process(Context& ctx) {
    if (!pso_ || !srb_ || !outputRTV_) return;

    auto* immediateCtx = ctx.immediateContext();

    ITextureView* rtvs[] = {outputRTV_};
    immediateCtx->SetRenderTargets(1, rtvs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    float clearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    immediateCtx->ClearRenderTarget(outputRTV_, clearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Viewport vp;
    vp.Width = static_cast<float>(outputWidth_);
    vp.Height = static_cast<float>(outputHeight_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    immediateCtx->SetViewports(1, &vp, outputWidth_, outputHeight_);

    updateUniforms(ctx);

    // Bind source and displacement map
    ITextureView* sourceTex = getInputSRV(0);
    ITextureView* dispTex = getInputSRV(1);

    if (sourceTex && srb_) {
        auto* srcVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_Source");
        if (srcVar) srcVar->Set(sourceTex);
    }

    if (dispTex && srb_) {
        auto* dispVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_DisplacementMap");
        if (dispVar) dispVar->Set(dispTex);
    }

    immediateCtx->SetPipelineState(pso_);
    immediateCtx->CommitShaderResources(srb_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    ctx.fullscreenQuad().draw();
}

} // namespace vivid
