// Composite Operator Implementation

#include "vivid/operators/composite.h"
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

// Embedded shader source for Composite
static const char* CompositePS_Source = R"(
cbuffer Constants : register(b0)
{
    int g_Mode;
    float g_Opacity;
    float2 padding;
};

Texture2D g_TextureA : register(t0);
Texture2D g_TextureB : register(t1);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(in PSInput input) : SV_TARGET
{
    float4 a = g_TextureA.Sample(g_Sampler, input.uv);
    float4 b = g_TextureB.Sample(g_Sampler, input.uv);

    // Apply opacity to foreground
    b.a *= g_Opacity;

    float4 result;

    if (g_Mode == 0) {
        // Porter-Duff "over" operation
        result.rgb = b.rgb * b.a + a.rgb * a.a * (1.0 - b.a);
        result.a = b.a + a.a * (1.0 - b.a);
        // Premultiplied to straight alpha
        if (result.a > 0.0) {
            result.rgb /= result.a;
        }
    }
    else if (g_Mode == 1) {
        // Additive
        result.rgb = a.rgb + b.rgb * b.a;
        result.a = saturate(a.a + b.a);
    }
    else if (g_Mode == 2) {
        // Multiply
        result.rgb = lerp(a.rgb, a.rgb * b.rgb, b.a);
        result.a = a.a;
    }
    else if (g_Mode == 3) {
        // Screen
        result.rgb = lerp(a.rgb, 1.0 - (1.0 - a.rgb) * (1.0 - b.rgb), b.a);
        result.a = a.a;
    }
    else {
        // Overlay
        float3 overlay;
        overlay = lerp(
            2.0 * a.rgb * b.rgb,
            1.0 - 2.0 * (1.0 - a.rgb) * (1.0 - b.rgb),
            step(0.5, a.rgb)
        );
        result.rgb = lerp(a.rgb, overlay, b.a);
        result.a = a.a;
    }

    return result;
}
)";

void Composite::createPipeline(Context& ctx) {
    // Create pixel shader from embedded source
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        CompositePS_Source,
        "CompositePS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) {
        return;
    }

    // Get fullscreen vertex shader
    IShader* vs = ctx.shaderUtils().getFullscreenVS();
    if (!vs) {
        ps->Release();
        return;
    }

    // Create pipeline with custom resource layout for two textures
    GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name = "CompositePSO";
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    psoCI.pVS = vs;
    psoCI.pPS = ps;

    // No vertex input
    psoCI.GraphicsPipeline.InputLayout.NumElements = 0;
    psoCI.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    psoCI.GraphicsPipeline.NumRenderTargets = 1;
    psoCI.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_BGRA8_UNORM_SRGB;  // Match macOS swap chain
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    psoCI.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;

    // Resource layout for two textures and constants
    ShaderResourceVariableDesc vars[] = {
        {SHADER_TYPE_PIXEL, "g_TextureA", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "g_TextureB", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
    };
    psoCI.PSODesc.ResourceLayout.Variables = vars;
    psoCI.PSODesc.ResourceLayout.NumVariables = 3;

    // Sampler
    SamplerDesc samplerDesc;
    samplerDesc.MinFilter = FILTER_TYPE_LINEAR;
    samplerDesc.MagFilter = FILTER_TYPE_LINEAR;
    samplerDesc.MipFilter = FILTER_TYPE_LINEAR;
    samplerDesc.AddressU = TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = TEXTURE_ADDRESS_CLAMP;

    ImmutableSamplerDesc immutableSamplers[] = {
        {SHADER_TYPE_PIXEL, "g_Sampler", samplerDesc}
    };
    psoCI.PSODesc.ResourceLayout.ImmutableSamplers = immutableSamplers;
    psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = 1;

    RefCntAutoPtr<IPipelineState> pso;
    ctx.device()->CreateGraphicsPipelineState(psoCI, &pso);

    ps->Release();

    if (!pso) {
        return;
    }

    pso_ = pso.Detach();

    // Create uniform buffer
    createUniformBuffer(ctx, sizeof(Constants));

    // Create SRB
    pso_->CreateShaderResourceBinding(&srb_, true);

    // Bind uniform buffer to SRB (dynamic variable)
    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) {
            cbVar->Set(uniformBuffer_);
        }
    }
}

void Composite::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->mode = mode_;
    cb->opacity = opacity_;
}

void Composite::process(Context& ctx) {
    if (!pso_ || !srb_ || !outputRTV_) {
        return;
    }

    auto* immediateCtx = ctx.immediateContext();

    // Set render target to our output texture
    ITextureView* rtvs[] = {outputRTV_};
    immediateCtx->SetRenderTargets(1, rtvs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Clear to transparent
    float clearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    immediateCtx->ClearRenderTarget(outputRTV_, clearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set viewport
    Viewport vp;
    vp.Width = static_cast<float>(outputWidth_);
    vp.Height = static_cast<float>(outputHeight_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    immediateCtx->SetViewports(1, &vp, outputWidth_, outputHeight_);

    // Update uniforms
    updateUniforms(ctx);

    // Bind input textures
    ITextureView* texA = getInputSRV(0);  // Background
    ITextureView* texB = getInputSRV(1);  // Foreground

    if (texA && srb_) {
        auto* varA = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureA");
        if (varA) varA->Set(texA);
    }

    if (texB && srb_) {
        auto* varB = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureB");
        if (varB) varB->Set(texB);
    }

    // Set pipeline and draw
    immediateCtx->SetPipelineState(pso_);
    immediateCtx->CommitShaderResources(srb_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Draw fullscreen triangle
    ctx.fullscreenQuad().draw();
}

} // namespace vivid
