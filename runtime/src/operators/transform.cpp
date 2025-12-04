// Transform Operator Implementation

#include "vivid/operators/transform.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

namespace vivid {

using namespace Diligent;

static const char* TransformPS_Source = R"(
cbuffer Constants : register(b0)
{
    float g_TranslateX;
    float g_TranslateY;
    float g_Rotate;
    float g_ScaleX;
    float g_ScaleY;
    float g_PivotX;
    float g_PivotY;
    float _pad;
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
    float2 uv = input.uv;
    float2 pivot = float2(g_PivotX, g_PivotY);

    // Move to pivot
    uv -= pivot;

    // Scale
    uv.x /= g_ScaleX;
    uv.y /= g_ScaleY;

    // Rotate
    float angle = -g_Rotate * PI / 180.0;
    float c = cos(angle);
    float s = sin(angle);
    uv = float2(
        uv.x * c - uv.y * s,
        uv.x * s + uv.y * c
    );

    // Move back from pivot
    uv += pivot;

    // Translate
    uv -= float2(g_TranslateX, g_TranslateY);

    // Sample with clamping
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    return g_Texture.Sample(g_Sampler, uv);
}
)";

void Transform::createPipeline(Context& ctx) {
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        TransformPS_Source,
        "TransformPS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) return;

    pso_ = ctx.shaderUtils().createFullscreenPipeline("TransformPSO", ps, true);
    ps->Release();

    if (!pso_) return;

    createUniformBuffer(ctx, sizeof(Constants));
    pso_->CreateShaderResourceBinding(&srb_, true);

    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) cbVar->Set(uniformBuffer_);
    }
}

void Transform::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->translateX = translateX_;
    cb->translateY = translateY_;
    cb->rotate = rotate_;
    cb->scaleX = scaleX_;
    cb->scaleY = scaleY_;
    cb->pivotX = pivotX_;
    cb->pivotY = pivotY_;
}

void Transform::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
