// Blur Operator Implementation

#include "vivid/operators/blur.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

namespace vivid {

using namespace Diligent;

// Simple box blur shader with weighted samples
static const char* BlurPS_Source = R"(
cbuffer Constants : register(b0)
{
    float g_Radius;
    float2 g_Resolution;
    float padding;
};

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// Simple 9-tap Gaussian weights
static const float weights[9] = {
    0.0162162162, 0.0540540541, 0.1216216216, 0.1945945946, 0.2270270270,
    0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162
};
static const float offsets[9] = {
    -4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0
};

float4 main(in PSInput input) : SV_TARGET
{
    float2 texelSize = 1.0 / g_Resolution;
    float4 result = float4(0.0, 0.0, 0.0, 0.0);

    // Horizontal blur
    float4 hBlur = float4(0.0, 0.0, 0.0, 0.0);
    for (int i = 0; i < 9; i++)
    {
        float2 offset = float2(offsets[i] * g_Radius * texelSize.x, 0.0);
        hBlur += g_Texture.Sample(g_Sampler, input.uv + offset) * weights[i];
    }

    // Vertical blur (approximate by sampling in a cross pattern)
    float4 vBlur = float4(0.0, 0.0, 0.0, 0.0);
    for (int j = 0; j < 9; j++)
    {
        float2 offset = float2(0.0, offsets[j] * g_Radius * texelSize.y);
        vBlur += g_Texture.Sample(g_Sampler, input.uv + offset) * weights[j];
    }

    // Average horizontal and vertical
    result = (hBlur + vBlur) * 0.5;

    return result;
}
)";

void Blur::createPipeline(Context& ctx) {
    // Create pixel shader
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        BlurPS_Source,
        "BlurPS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) {
        return;
    }

    // Create pipeline with input texture
    pso_ = ctx.shaderUtils().createFullscreenPipeline("BlurPSO", ps, true);

    ps->Release();

    if (!pso_) {
        return;
    }

    // Create uniform buffer
    createUniformBuffer(ctx, sizeof(Constants));

    // Create SRB
    pso_->CreateShaderResourceBinding(&srb_, true);

    // Bind uniform buffer
    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) {
            cbVar->Set(uniformBuffer_);
        }
    }
}

void Blur::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->radius = radius_;
    cb->resolution[0] = static_cast<float>(ctx.width());
    cb->resolution[1] = static_cast<float>(ctx.height());
}

void Blur::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
