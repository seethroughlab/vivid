// Passthrough texture shader - samples and displays a texture
// Input: fullscreen triangle UV coordinates
// Output: sampled texture color

Texture2D g_Texture;
SamplerState g_Sampler;

cbuffer Constants {
    float4 u_Tint;      // Color tint multiplier
    float u_Opacity;    // Overall opacity
    float u_Time;       // Time for effects
    float2 u_Scale;     // UV scale
    float2 u_Offset;    // UV offset
    float2 _padding;
};

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    // Apply UV transform
    float2 uv = input.uv * u_Scale + u_Offset;

    // Sample texture
    float4 color = g_Texture.Sample(g_Sampler, uv);

    // Apply tint and opacity
    color.rgb *= u_Tint.rgb;
    color.a *= u_Tint.a * u_Opacity;

    return color;
}
