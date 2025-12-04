// Output Operator Implementation
// Renders a texture to the swap chain

#include "vivid/operators/output.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "RefCntAutoPtr.hpp"

namespace vivid {

using namespace Diligent;

// Simple passthrough shader
static const char* OutputPS_Source = R"(
Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(in PSInput input) : SV_TARGET
{
    return g_Texture.Sample(g_Sampler, input.uv);
}
)";

void Output::init(Context& ctx) {
    // Create pixel shader
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        OutputPS_Source,
        "OutputPS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) {
        return;
    }

    // Get the swap chain's actual render target format
    const auto& scDesc = ctx.swapChain()->GetDesc();
    TEXTURE_FORMAT rtFormat = scDesc.ColorBufferFormat;

    // Create pipeline with matching format (no depth buffer)
    pso_ = ctx.shaderUtils().createOutputPipeline("OutputPSO", ps, rtFormat);

    ps->Release();

    if (!pso_) {
        return;
    }

    // Create SRB
    pso_->CreateShaderResourceBinding(&srb_, true);
}

void Output::cleanup() {
    if (srb_) {
        srb_->Release();
        srb_ = nullptr;
    }
    if (pso_) {
        pso_->Release();
        pso_ = nullptr;
    }
}

void Output::process(Context& ctx) {
    if (!pso_ || !srb_) {
        return;
    }

    auto* immediateCtx = ctx.immediateContext();

    // Set render target to swap chain back buffer (no depth buffer needed for 2D output)
    auto* rtv = ctx.currentRTV();
    immediateCtx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set viewport
    Viewport vp;
    vp.Width = static_cast<float>(ctx.width());
    vp.Height = static_cast<float>(ctx.height());
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    immediateCtx->SetViewports(1, &vp, ctx.width(), ctx.height());

    // Bind input texture
    ITextureView* inputSRV = getInputSRV(0);
    if (inputSRV && srb_) {
        auto* texVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture");
        if (texVar) {
            texVar->Set(inputSRV);
        }
    }

    // Set pipeline and draw
    immediateCtx->SetPipelineState(pso_);
    immediateCtx->CommitShaderResources(srb_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Draw fullscreen triangle
    ctx.fullscreenQuad().draw();
}

} // namespace vivid
