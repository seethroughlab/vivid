// Transform shader - Translate, Rotate, Scale with pivot and repeat modes

Texture2D g_Texture;
SamplerState g_Sampler;

cbuffer Constants {
    float2 u_Translate;     // Translation offset
    float u_Rotate;         // Rotation in radians
    float2 u_Scale;         // Scale factors
    float2 u_Pivot;         // Pivot point (0.5, 0.5 = center)
    int u_RepeatMode;       // 0=clamp, 1=repeat, 2=mirror
    float3 u_BorderColor;   // Border color when clamping
    float _padding;
};

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float2 rotate2D(float2 p, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

float2 applyRepeatMode(float2 uv, int mode) {
    if (mode == 1) {
        // Repeat
        return frac(uv);
    } else if (mode == 2) {
        // Mirror
        float2 m = abs(fmod(uv, 2.0));
        return float2(
            m.x > 1.0 ? 2.0 - m.x : m.x,
            m.y > 1.0 ? 2.0 - m.y : m.y
        );
    }
    // Clamp (default)
    return uv;
}

float4 main(PSInput input) : SV_Target {
    float2 uv = input.uv;

    // Move to pivot
    uv -= u_Pivot;

    // Apply scale
    uv /= max(u_Scale, 0.001);

    // Apply rotation
    uv = rotate2D(uv, -u_Rotate);

    // Apply translation
    uv -= u_Translate;

    // Move back from pivot
    uv += u_Pivot;

    // Check if out of bounds (before applying repeat mode)
    bool outOfBounds = uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0;

    // Apply repeat mode
    uv = applyRepeatMode(uv, u_RepeatMode);

    // Sample texture
    float4 color = g_Texture.Sample(g_Sampler, uv);

    // Apply border color for clamp mode when out of bounds
    if (u_RepeatMode == 0 && outOfBounds) {
        color.rgb = u_BorderColor;
        color.a = 0.0;
    }

    return color;
}
