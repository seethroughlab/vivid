// Brightness, Contrast, Exposure, and Gamma adjustment

Texture2D g_Texture;
SamplerState g_Sampler;

cbuffer Constants {
    float u_Brightness;     // -1 to 1 (0 = no change)
    float u_Contrast;       // -1 to 1 (0 = no change)
    float u_Exposure;       // -5 to 5 (0 = no change)
    float u_Gamma;          // 0.1 to 5 (1 = no change)
    float u_Saturation;     // 0 to 2 (1 = no change)
    float3 _padding;
};

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Convert RGB to luminance
float luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float4 main(PSInput input) : SV_Target {
    float4 color = g_Texture.Sample(g_Sampler, input.uv);

    // Apply exposure (before other adjustments)
    color.rgb *= pow(2.0, u_Exposure);

    // Apply brightness (additive)
    color.rgb += u_Brightness;

    // Apply contrast (centered at 0.5)
    float contrastFactor = 1.0 + u_Contrast;
    color.rgb = (color.rgb - 0.5) * contrastFactor + 0.5;

    // Apply saturation
    float lum = luminance(color.rgb);
    color.rgb = lerp(float3(lum, lum, lum), color.rgb, u_Saturation);

    // Apply gamma correction
    color.rgb = pow(max(color.rgb, 0.0), 1.0 / u_Gamma);

    // Clamp final result
    color.rgb = saturate(color.rgb);

    return color;
}
