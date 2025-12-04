// Noise Operator Implementation

#include "vivid/operators/noise.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

namespace vivid {

using namespace Diligent;

// Embedded shader source for Noise
// Uses simplex noise with fractal Brownian motion (fBm)
static const char* NoisePS_Source = R"(
cbuffer Constants : register(b0)
{
    float g_Scale;
    float g_Time;
    int g_Octaves;
    float g_Lacunarity;
    float g_Persistence;
    float _pad0;
    float _pad1;
    float _pad2;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// Simplex noise helper functions
float3 mod289(float3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
float4 mod289(float4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
float4 permute(float4 x) { return mod289(((x * 34.0) + 1.0) * x); }
float4 taylorInvSqrt(float4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

// 3D Simplex noise
float snoise(float3 v)
{
    const float2 C = float2(1.0 / 6.0, 1.0 / 3.0);
    const float4 D = float4(0.0, 0.5, 1.0, 2.0);

    // First corner
    float3 i = floor(v + dot(v, C.yyy));
    float3 x0 = v - i + dot(i, C.xxx);

    // Other corners
    float3 g = step(x0.yzx, x0.xyz);
    float3 l = 1.0 - g;
    float3 i1 = min(g.xyz, l.zxy);
    float3 i2 = max(g.xyz, l.zxy);

    float3 x1 = x0 - i1 + C.xxx;
    float3 x2 = x0 - i2 + C.yyy;
    float3 x3 = x0 - D.yyy;

    // Permutations
    i = mod289(i);
    float4 p = permute(permute(permute(
        i.z + float4(0.0, i1.z, i2.z, 1.0))
        + i.y + float4(0.0, i1.y, i2.y, 1.0))
        + i.x + float4(0.0, i1.x, i2.x, 1.0));

    // Gradients
    float n_ = 0.142857142857;
    float3 ns = n_ * D.wyz - D.xzx;

    float4 j = p - 49.0 * floor(p * ns.z * ns.z);

    float4 x_ = floor(j * ns.z);
    float4 y_ = floor(j - 7.0 * x_);

    float4 x = x_ * ns.x + ns.yyyy;
    float4 y = y_ * ns.x + ns.yyyy;
    float4 h = 1.0 - abs(x) - abs(y);

    float4 b0 = float4(x.xy, y.xy);
    float4 b1 = float4(x.zw, y.zw);

    float4 s0 = floor(b0) * 2.0 + 1.0;
    float4 s1 = floor(b1) * 2.0 + 1.0;
    float4 sh = -step(h, float4(0.0, 0.0, 0.0, 0.0));

    float4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    float4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    float3 p0 = float3(a0.xy, h.x);
    float3 p1 = float3(a0.zw, h.y);
    float3 p2 = float3(a1.xy, h.z);
    float3 p3 = float3(a1.zw, h.w);

    // Normalize gradients
    float4 norm = taylorInvSqrt(float4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    // Mix contributions
    float4 m = max(0.6 - float4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, float4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

// Fractal Brownian Motion
float fbm(float3 p, int octaves, float lacunarity, float persistence)
{
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++)
    {
        value += amplitude * snoise(p * frequency);
        frequency *= lacunarity;
        amplitude *= persistence;
    }

    return value;
}

float4 main(in PSInput input) : SV_TARGET
{
    float3 p = float3(input.uv * g_Scale, g_Time);
    float n = fbm(p, g_Octaves, g_Lacunarity, g_Persistence);

    // Map from [-1, 1] to [0, 1]
    n = n * 0.5 + 0.5;

    return float4(n, n, n, 1.0);
}
)";

void Noise::createPipeline(Context& ctx) {
    // Create pixel shader from embedded source
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        NoisePS_Source,
        "NoisePS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) {
        return;
    }

    // Create pipeline - no input texture needed
    pso_ = ctx.shaderUtils().createFullscreenPipeline("NoisePSO", ps, false);

    ps->Release();

    if (!pso_) {
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

void Noise::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->scale = scale_;
    cb->time = ctx.time() * speed_;
    cb->octaves = octaves_;
    cb->lacunarity = lacunarity_;
    cb->persistence = persistence_;
}

void Noise::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
