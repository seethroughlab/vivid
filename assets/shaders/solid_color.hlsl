// SolidColor Pixel Shader
// Outputs a constant color for every pixel

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
