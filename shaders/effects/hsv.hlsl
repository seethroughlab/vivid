// HSV (Hue/Saturation/Value) adjustment
// Supports hue shift, saturation, value, and colorize mode

Texture2D g_Texture;
SamplerState g_Sampler;

cbuffer Constants {
    float u_HueShift;       // -1 to 1 (maps to -180 to 180 degrees)
    float u_Saturation;     // 0 to 2 (1 = no change)
    float u_Value;          // 0 to 2 (1 = no change)
    int u_Colorize;         // 0 = adjust, 1 = colorize mode
    float3 u_ColorizeHSV;   // Colorize target (H, S, V)
    float _padding;
};

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// RGB to HSV conversion
float3 rgb2hsv(float3 c) {
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// HSV to RGB conversion
float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

float4 main(PSInput input) : SV_Target {
    float4 color = g_Texture.Sample(g_Sampler, input.uv);

    // Convert to HSV
    float3 hsv = rgb2hsv(color.rgb);

    if (u_Colorize != 0) {
        // Colorize mode: replace hue and saturation, preserve value
        hsv.x = u_ColorizeHSV.x;
        hsv.y = u_ColorizeHSV.y;
        hsv.z *= u_ColorizeHSV.z;
    } else {
        // Adjustment mode
        // Shift hue (wrapping)
        hsv.x = frac(hsv.x + u_HueShift * 0.5 + 1.0);

        // Adjust saturation
        hsv.y *= u_Saturation;

        // Adjust value
        hsv.z *= u_Value;
    }

    // Clamp saturation and value
    hsv.y = saturate(hsv.y);
    hsv.z = saturate(hsv.z);

    // Convert back to RGB
    color.rgb = hsv2rgb(hsv);

    return color;
}
