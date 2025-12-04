// HSV Operator Implementation

#include "vivid/operators/hsv.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

namespace vivid {

using namespace Diligent;

static const char* HSVPS_Source = R"(
cbuffer Constants : register(b0)
{
    float g_HueShift;
    float g_Saturation;
    float g_Value;
    float _pad;
};

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float3 rgb2hsv(float3 c)
{
    float4 K = float4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 hsv2rgb(float3 c)
{
    float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

float4 main(in PSInput input) : SV_TARGET
{
    float4 color = g_Texture.Sample(g_Sampler, input.uv);

    // Convert to HSV
    float3 hsv = rgb2hsv(color.rgb);

    // Apply adjustments
    hsv.x = frac(hsv.x + g_HueShift / 360.0);  // Hue shift (in degrees)
    hsv.y *= g_Saturation;  // Saturation multiply
    hsv.z *= g_Value;       // Value multiply

    // Convert back to RGB
    color.rgb = hsv2rgb(hsv);

    return color;
}
)";

void HSV::createPipeline(Context& ctx) {
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        HSVPS_Source,
        "HSVPS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) return;

    pso_ = ctx.shaderUtils().createFullscreenPipeline("HSVPSO", ps, true);
    ps->Release();

    if (!pso_) return;

    createUniformBuffer(ctx, sizeof(Constants));
    pso_->CreateShaderResourceBinding(&srb_, true);

    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) cbVar->Set(uniformBuffer_);
    }
}

void HSV::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->hueShift = hueShift_;
    cb->saturation = saturation_;
    cb->value = value_;
}

void HSV::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
