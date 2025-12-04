// SolidColor Operator Implementation

#include "vivid/operators/solid_color.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

#include <iostream>

namespace vivid {

using namespace Diligent;

// Embedded shader source for SolidColor
static const char* SolidColorPS_Source = R"(
cbuffer Constants : register(b0)
{
    float4 g_Color;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(in PSInput input) : SV_TARGET
{
    return g_Color;
}
)";

void SolidColor::createPipeline(Context& ctx) {
    // Create pixel shader from embedded source
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        SolidColorPS_Source,
        "SolidColorPS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) {
        std::cerr << "SolidColor: Failed to compile shader" << std::endl;
        return;
    }

    // Create pipeline - no input texture needed
    pso_ = ctx.shaderUtils().createFullscreenPipeline("SolidColorPSO", ps, false);

    ps->Release();

    if (!pso_) {
        std::cerr << "SolidColor: Failed to create PSO" << std::endl;
        return;
    }

    // Create uniform buffer
    createUniformBuffer(ctx, sizeof(Constants));

    // Create SRB
    pso_->CreateShaderResourceBinding(&srb_, true);

    // Bind uniform buffer to SRB
    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) {
            cbVar->Set(uniformBuffer_);
        }
    }
}

void SolidColor::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->color[0] = color_.r;
    cb->color[1] = color_.g;
    cb->color[2] = color_.b;
    cb->color[3] = color_.a;
}

void SolidColor::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
