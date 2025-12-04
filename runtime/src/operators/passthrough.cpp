// Passthrough Operator Implementation

#include "vivid/operators/passthrough.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "DeviceContext.h"

namespace vivid {

using namespace Diligent;

// Simple passthrough shader
static const char* PassthroughPS_Source = R"(
Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(in PSInput input) : SV_TARGET
{
    return g_Texture.Sample(g_Sampler, input.uv);
}
)";

void Passthrough::createPipeline(Context& ctx) {
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        PassthroughPS_Source,
        "PassthroughPS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) return;

    pso_ = ctx.shaderUtils().createFullscreenPipeline("PassthroughPSO", ps, true);
    ps->Release();

    if (!pso_) return;

    pso_->CreateShaderResourceBinding(&srb_, true);
}

void Passthrough::updateUniforms(Context& ctx) {
    // No uniforms
}

void Passthrough::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
