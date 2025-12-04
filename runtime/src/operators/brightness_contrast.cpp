// Brightness/Contrast Operator Implementation

#include "vivid/operators/brightness_contrast.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

namespace vivid {

using namespace Diligent;

static const char* BrightnessContrastPS_Source = R"(
cbuffer Constants : register(b0)
{
    float g_Brightness;
    float g_Contrast;
    float2 _pad;
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
    float4 color = g_Texture.Sample(g_Sampler, input.uv);

    // Apply brightness (additive)
    color.rgb += g_Brightness;

    // Apply contrast (multiply around middle gray)
    color.rgb = (color.rgb - 0.5) * g_Contrast + 0.5;

    // Clamp result
    color.rgb = saturate(color.rgb);

    return color;
}
)";

void BrightnessContrast::createPipeline(Context& ctx) {
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        BrightnessContrastPS_Source,
        "BrightnessContrastPS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) return;

    pso_ = ctx.shaderUtils().createFullscreenPipeline("BrightnessContrastPSO", ps, true);
    ps->Release();

    if (!pso_) return;

    createUniformBuffer(ctx, sizeof(Constants));
    pso_->CreateShaderResourceBinding(&srb_, true);

    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) cbVar->Set(uniformBuffer_);
    }
}

void BrightnessContrast::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->brightness = brightness_;
    cb->contrast = contrast_;
}

void BrightnessContrast::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
