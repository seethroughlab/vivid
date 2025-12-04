// Edge Detection Operator Implementation

#include "vivid/operators/edge_detect.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

namespace vivid {

using namespace Diligent;

static const char* EdgeDetectPS_Source = R"(
cbuffer Constants : register(b0)
{
    float2 g_Resolution;
    int g_Mode;
    float g_Strength;
    float g_Threshold;
    float _pad0;
    float _pad1;
    float _pad2;
};

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float luminance(float3 c)
{
    return dot(c, float3(0.299, 0.587, 0.114));
}

float4 main(in PSInput input) : SV_TARGET
{
    float2 texelSize = 1.0 / g_Resolution;
    float2 uv = input.uv;

    // Sample 3x3 neighborhood
    float tl = luminance(g_Texture.Sample(g_Sampler, uv + texelSize * float2(-1, -1)).rgb);
    float tc = luminance(g_Texture.Sample(g_Sampler, uv + texelSize * float2( 0, -1)).rgb);
    float tr = luminance(g_Texture.Sample(g_Sampler, uv + texelSize * float2( 1, -1)).rgb);
    float ml = luminance(g_Texture.Sample(g_Sampler, uv + texelSize * float2(-1,  0)).rgb);
    float mc = luminance(g_Texture.Sample(g_Sampler, uv + texelSize * float2( 0,  0)).rgb);
    float mr = luminance(g_Texture.Sample(g_Sampler, uv + texelSize * float2( 1,  0)).rgb);
    float bl = luminance(g_Texture.Sample(g_Sampler, uv + texelSize * float2(-1,  1)).rgb);
    float bc = luminance(g_Texture.Sample(g_Sampler, uv + texelSize * float2( 0,  1)).rgb);
    float br = luminance(g_Texture.Sample(g_Sampler, uv + texelSize * float2( 1,  1)).rgb);

    float gx, gy;

    if (g_Mode == 0) {
        // Sobel
        gx = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
        gy = -tl - 2.0*tc - tr + bl + 2.0*bc + br;
    }
    else if (g_Mode == 1) {
        // Prewitt
        gx = -tl - ml - bl + tr + mr + br;
        gy = -tl - tc - tr + bl + bc + br;
    }
    else {
        // Laplacian
        float laplacian = -8.0*mc + tl + tc + tr + ml + mr + bl + bc + br;
        gx = laplacian;
        gy = 0.0;
    }

    float edge = sqrt(gx*gx + gy*gy) * g_Strength;

    // Apply threshold
    if (edge < g_Threshold) {
        edge = 0.0;
    }

    edge = saturate(edge);

    return float4(edge, edge, edge, 1.0);
}
)";

void EdgeDetect::createPipeline(Context& ctx) {
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        EdgeDetectPS_Source,
        "EdgeDetectPS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) return;

    pso_ = ctx.shaderUtils().createFullscreenPipeline("EdgeDetectPSO", ps, true);
    ps->Release();

    if (!pso_) return;

    createUniformBuffer(ctx, sizeof(Constants));
    pso_->CreateShaderResourceBinding(&srb_, true);

    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) cbVar->Set(uniformBuffer_);
    }
}

void EdgeDetect::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->resolution[0] = static_cast<float>(ctx.width());
    cb->resolution[1] = static_cast<float>(ctx.height());
    cb->mode = static_cast<int>(mode_);
    cb->strength = strength_;
    cb->threshold = threshold_;
}

void EdgeDetect::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
