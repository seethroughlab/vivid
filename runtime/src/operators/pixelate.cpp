// Pixelate Operator Implementation

#include "vivid/operators/pixelate.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

namespace vivid {

using namespace Diligent;

static const char* PixelatePS_Source = R"(
cbuffer Constants : register(b0)
{
    float2 g_Resolution;
    float g_PixelSize;
    float _pad;
};

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(in PSInput input) : SV_TARGET
{
    float2 pixelSize = g_PixelSize / g_Resolution;

    // Snap UV to pixel grid
    float2 snappedUV = floor(input.uv / pixelSize) * pixelSize + pixelSize * 0.5;

    return g_Texture.Sample(g_Sampler, snappedUV);
}
)";

void Pixelate::createPipeline(Context& ctx) {
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        PixelatePS_Source,
        "PixelatePS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) return;

    pso_ = ctx.shaderUtils().createFullscreenPipeline("PixelatePSO", ps, true);
    ps->Release();

    if (!pso_) return;

    createUniformBuffer(ctx, sizeof(Constants));
    pso_->CreateShaderResourceBinding(&srb_, true);

    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) cbVar->Set(uniformBuffer_);
    }
}

void Pixelate::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->resolution[0] = static_cast<float>(ctx.width());
    cb->resolution[1] = static_cast<float>(ctx.height());
    cb->pixelSize = pixelSize_;
}

void Pixelate::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
