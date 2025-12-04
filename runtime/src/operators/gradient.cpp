// Gradient Operator Implementation

#include "vivid/operators/gradient.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

namespace vivid {

using namespace Diligent;

static const char* GradientPS_Source = R"(
cbuffer Constants : register(b0)
{
    float4 g_ColorA;
    float4 g_ColorB;
    int g_Type;
    float g_Angle;
    float g_CenterX;
    float g_CenterY;
    float g_Scale;
    float _pad0;
    float _pad1;
    float _pad2;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static const float PI = 3.14159265359;

float4 main(in PSInput input) : SV_TARGET
{
    float2 uv = input.uv;
    float2 center = float2(g_CenterX, g_CenterY);
    float t = 0.0;

    if (g_Type == 0) {
        // Linear gradient
        float angle = g_Angle * PI / 180.0;
        float2 dir = float2(cos(angle), sin(angle));
        t = dot(uv - 0.5, dir) * g_Scale + 0.5;
    }
    else if (g_Type == 1) {
        // Radial gradient
        float dist = length(uv - center) * 2.0 * g_Scale;
        t = dist;
    }
    else if (g_Type == 2) {
        // Angular gradient
        float2 delta = uv - center;
        float angle = atan2(delta.y, delta.x);
        t = (angle + PI + g_Angle * PI / 180.0) / (2.0 * PI);
        t = frac(t * g_Scale);
    }
    else {
        // Diamond gradient
        float2 delta = abs(uv - center);
        t = (delta.x + delta.y) * g_Scale;
    }

    t = saturate(t);
    return lerp(g_ColorA, g_ColorB, t);
}
)";

void Gradient::createPipeline(Context& ctx) {
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        GradientPS_Source,
        "GradientPS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) return;

    pso_ = ctx.shaderUtils().createFullscreenPipeline("GradientPSO", ps, false);
    ps->Release();

    if (!pso_) return;

    createUniformBuffer(ctx, sizeof(Constants));
    pso_->CreateShaderResourceBinding(&srb_, true);

    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) cbVar->Set(uniformBuffer_);
    }
}

void Gradient::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->colorA[0] = colorA_.r;
    cb->colorA[1] = colorA_.g;
    cb->colorA[2] = colorA_.b;
    cb->colorA[3] = colorA_.a;
    cb->colorB[0] = colorB_.r;
    cb->colorB[1] = colorB_.g;
    cb->colorB[2] = colorB_.b;
    cb->colorB[3] = colorB_.a;
    cb->type = static_cast<int>(type_);
    cb->angle = angle_;
    cb->centerX = centerX_;
    cb->centerY = centerY_;
    cb->scale = scale_;
}

void Gradient::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
