// Mirror/Kaleidoscope Operator Implementation

#include "vivid/operators/mirror.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

namespace vivid {

using namespace Diligent;

static const char* MirrorPS_Source = R"(
cbuffer Constants : register(b0)
{
    int g_Mode;
    int g_Segments;
    float g_Angle;
    float g_CenterX;
    float g_CenterY;
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

static const float PI = 3.14159265359;

float4 main(in PSInput input) : SV_TARGET
{
    float2 uv = input.uv;
    float2 center = float2(g_CenterX, g_CenterY);

    if (g_Mode == 0) {
        // Horizontal mirror
        if (uv.x > 0.5) {
            uv.x = 1.0 - uv.x;
        }
    }
    else if (g_Mode == 1) {
        // Vertical mirror
        if (uv.y > 0.5) {
            uv.y = 1.0 - uv.y;
        }
    }
    else if (g_Mode == 2) {
        // Both (quad mirror)
        if (uv.x > 0.5) uv.x = 1.0 - uv.x;
        if (uv.y > 0.5) uv.y = 1.0 - uv.y;
    }
    else if (g_Mode == 3) {
        // Quad with center
        uv = abs(uv - center) + center;
        uv = min(uv, 2.0 * center - uv);
    }
    else {
        // Kaleidoscope
        float2 delta = uv - center;
        float r = length(delta);
        float theta = atan2(delta.y, delta.x) + g_Angle * PI / 180.0;

        // Divide into segments
        float segmentAngle = 2.0 * PI / float(g_Segments);
        float segment = floor(theta / segmentAngle);
        float localAngle = theta - segment * segmentAngle;

        // Mirror alternating segments
        if (fmod(segment, 2.0) >= 1.0) {
            localAngle = segmentAngle - localAngle;
        }

        uv = center + r * float2(cos(localAngle), sin(localAngle));
    }

    // Clamp to valid range
    uv = saturate(uv);

    return g_Texture.Sample(g_Sampler, uv);
}
)";

void Mirror::createPipeline(Context& ctx) {
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        MirrorPS_Source,
        "MirrorPS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) return;

    pso_ = ctx.shaderUtils().createFullscreenPipeline("MirrorPSO", ps, true);
    ps->Release();

    if (!pso_) return;

    createUniformBuffer(ctx, sizeof(Constants));
    pso_->CreateShaderResourceBinding(&srb_, true);

    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) cbVar->Set(uniformBuffer_);
    }
}

void Mirror::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->mode = static_cast<int>(mode_);
    cb->segments = segments_;
    cb->angle = angle_;
    cb->centerX = centerX_;
    cb->centerY = centerY_;
}

void Mirror::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
