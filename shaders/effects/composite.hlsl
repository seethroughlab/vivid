// Composite shader - Blend two textures with various blend modes

Texture2D g_TextureA;   // Base/bottom layer
Texture2D g_TextureB;   // Blend/top layer
SamplerState g_Sampler;

cbuffer Constants {
    int u_BlendMode;        // Blend mode (0-15)
    float u_Opacity;        // Blend opacity (0-1)
    float2 _padding;
};

// Blend modes:
// 0  = Normal
// 1  = Add
// 2  = Subtract
// 3  = Multiply
// 4  = Screen
// 5  = Overlay
// 6  = Soft Light
// 7  = Hard Light
// 8  = Color Dodge
// 9  = Color Burn
// 10 = Difference
// 11 = Exclusion
// 12 = Lighten
// 13 = Darken
// 14 = Linear Burn
// 15 = Vivid Light

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Blend mode implementations
float3 blendNormal(float3 base, float3 blend) {
    return blend;
}

float3 blendAdd(float3 base, float3 blend) {
    return min(base + blend, 1.0);
}

float3 blendSubtract(float3 base, float3 blend) {
    return max(base - blend, 0.0);
}

float3 blendMultiply(float3 base, float3 blend) {
    return base * blend;
}

float3 blendScreen(float3 base, float3 blend) {
    return 1.0 - (1.0 - base) * (1.0 - blend);
}

float blendOverlay(float base, float blend) {
    return base < 0.5 ? (2.0 * base * blend) : (1.0 - 2.0 * (1.0 - base) * (1.0 - blend));
}

float3 blendOverlay3(float3 base, float3 blend) {
    return float3(blendOverlay(base.r, blend.r), blendOverlay(base.g, blend.g), blendOverlay(base.b, blend.b));
}

float blendSoftLight(float base, float blend) {
    return (blend < 0.5) ?
        (2.0 * base * blend + base * base * (1.0 - 2.0 * blend)) :
        (sqrt(base) * (2.0 * blend - 1.0) + 2.0 * base * (1.0 - blend));
}

float3 blendSoftLight3(float3 base, float3 blend) {
    return float3(blendSoftLight(base.r, blend.r), blendSoftLight(base.g, blend.g), blendSoftLight(base.b, blend.b));
}

float blendHardLight(float base, float blend) {
    return blend < 0.5 ? (2.0 * base * blend) : (1.0 - 2.0 * (1.0 - base) * (1.0 - blend));
}

float3 blendHardLight3(float3 base, float3 blend) {
    return float3(blendHardLight(base.r, blend.r), blendHardLight(base.g, blend.g), blendHardLight(base.b, blend.b));
}

float3 blendColorDodge(float3 base, float3 blend) {
    return min(base / max(1.0 - blend, 0.001), 1.0);
}

float3 blendColorBurn(float3 base, float3 blend) {
    return max(1.0 - (1.0 - base) / max(blend, 0.001), 0.0);
}

float3 blendDifference(float3 base, float3 blend) {
    return abs(base - blend);
}

float3 blendExclusion(float3 base, float3 blend) {
    return base + blend - 2.0 * base * blend;
}

float3 blendLighten(float3 base, float3 blend) {
    return max(base, blend);
}

float3 blendDarken(float3 base, float3 blend) {
    return min(base, blend);
}

float3 blendLinearBurn(float3 base, float3 blend) {
    return max(base + blend - 1.0, 0.0);
}

float blendVividLight(float base, float blend) {
    return blend < 0.5 ?
        max(1.0 - (1.0 - base) / max(2.0 * blend, 0.001), 0.0) :
        min(base / max(2.0 * (1.0 - blend), 0.001), 1.0);
}

float3 blendVividLight3(float3 base, float3 blend) {
    return float3(blendVividLight(base.r, blend.r), blendVividLight(base.g, blend.g), blendVividLight(base.b, blend.b));
}

float4 main(PSInput input) : SV_Target {
    float4 colorA = g_TextureA.Sample(g_Sampler, input.uv);
    float4 colorB = g_TextureB.Sample(g_Sampler, input.uv);

    float3 result;

    switch (u_BlendMode) {
        case 0:  result = blendNormal(colorA.rgb, colorB.rgb); break;
        case 1:  result = blendAdd(colorA.rgb, colorB.rgb); break;
        case 2:  result = blendSubtract(colorA.rgb, colorB.rgb); break;
        case 3:  result = blendMultiply(colorA.rgb, colorB.rgb); break;
        case 4:  result = blendScreen(colorA.rgb, colorB.rgb); break;
        case 5:  result = blendOverlay3(colorA.rgb, colorB.rgb); break;
        case 6:  result = blendSoftLight3(colorA.rgb, colorB.rgb); break;
        case 7:  result = blendHardLight3(colorA.rgb, colorB.rgb); break;
        case 8:  result = blendColorDodge(colorA.rgb, colorB.rgb); break;
        case 9:  result = blendColorBurn(colorA.rgb, colorB.rgb); break;
        case 10: result = blendDifference(colorA.rgb, colorB.rgb); break;
        case 11: result = blendExclusion(colorA.rgb, colorB.rgb); break;
        case 12: result = blendLighten(colorA.rgb, colorB.rgb); break;
        case 13: result = blendDarken(colorA.rgb, colorB.rgb); break;
        case 14: result = blendLinearBurn(colorA.rgb, colorB.rgb); break;
        case 15: result = blendVividLight3(colorA.rgb, colorB.rgb); break;
        default: result = colorB.rgb; break;
    }

    // Apply opacity
    result = lerp(colorA.rgb, result, u_Opacity * colorB.a);

    // Preserve base alpha
    return float4(result, colorA.a);
}
