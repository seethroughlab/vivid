// Chromatic Aberration Operator Implementation

#include "vivid/operators/chromatic_aberration.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

namespace vivid {

using namespace Diligent;

static const char* ChromaticAberrationPS_Source = R"(
cbuffer Constants : register(b0)
{
    float g_Amount;
    float g_Angle;
    float g_CenterX;
    float g_CenterY;
};

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static const float PI = 3.14159265359;

float4 main(in PSInput input) : SV_TARGET
{
    float2 center = float2(g_CenterX, g_CenterY);
    float2 dir = input.uv - center;
    float dist = length(dir);

    // Direction based on angle
    float angle = g_Angle * PI / 180.0;
    float2 offset = float2(cos(angle), sin(angle)) * g_Amount * dist;

    // Sample each channel with different offsets
    float r = g_Texture.Sample(g_Sampler, input.uv + offset).r;
    float g = g_Texture.Sample(g_Sampler, input.uv).g;
    float b = g_Texture.Sample(g_Sampler, input.uv - offset).b;
    float a = g_Texture.Sample(g_Sampler, input.uv).a;

    return float4(r, g, b, a);
}
)";

void ChromaticAberration::createPipeline(Context& ctx) {
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        ChromaticAberrationPS_Source,
        "ChromaticAberrationPS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) return;

    pso_ = ctx.shaderUtils().createFullscreenPipeline("ChromaticAberrationPSO", ps, true);
    ps->Release();

    if (!pso_) return;

    createUniformBuffer(ctx, sizeof(Constants));
    pso_->CreateShaderResourceBinding(&srb_, true);

    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) cbVar->Set(uniformBuffer_);
    }
}

void ChromaticAberration::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->amount = amount_;
    cb->angle = angle_;
    cb->centerX = centerX_;
    cb->centerY = centerY_;
}

void ChromaticAberration::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
