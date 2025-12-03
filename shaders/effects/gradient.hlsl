// Gradient generator - Linear, Radial, Angular, and Diamond gradients

cbuffer Constants {
    int u_GradientType;     // 0=linear, 1=radial, 2=angular, 3=diamond
    float u_Angle;          // Gradient angle (for linear)
    float2 u_Center;        // Gradient center (for radial/angular)
    float u_Scale;          // Gradient scale
    float u_Offset;         // Gradient offset
    int u_Repeat;           // 0=clamp, 1=repeat, 2=mirror
    float4 u_ColorA;        // Start color
    float4 u_ColorB;        // End color
    float4 u_ColorC;        // Middle color (optional, if u_UseMiddle)
    int u_UseMiddle;        // Use 3-color gradient
    float u_MiddlePos;      // Position of middle color (0-1)
    float2 _padding;
};

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float applyRepeat(float t, int mode) {
    if (mode == 1) {
        // Repeat
        return frac(t);
    } else if (mode == 2) {
        // Mirror
        float m = abs(fmod(t, 2.0));
        return m > 1.0 ? 2.0 - m : m;
    }
    // Clamp
    return saturate(t);
}

float4 sampleGradient(float t) {
    t = saturate(t);

    if (u_UseMiddle != 0) {
        // 3-color gradient
        if (t < u_MiddlePos) {
            return lerp(u_ColorA, u_ColorC, t / u_MiddlePos);
        } else {
            return lerp(u_ColorC, u_ColorB, (t - u_MiddlePos) / (1.0 - u_MiddlePos));
        }
    }

    // 2-color gradient
    return lerp(u_ColorA, u_ColorB, t);
}

float4 main(PSInput input) : SV_Target {
    float2 uv = input.uv;
    float t;

    switch (u_GradientType) {
        case 0: {
            // Linear gradient
            float2 dir = float2(cos(u_Angle), sin(u_Angle));
            float2 centered = uv - 0.5;
            t = dot(centered, dir) + 0.5;
            break;
        }

        case 1: {
            // Radial gradient
            float2 centered = uv - u_Center;
            t = length(centered) * 2.0;
            break;
        }

        case 2: {
            // Angular gradient
            float2 centered = uv - u_Center;
            t = (atan2(centered.y, centered.x) + 3.14159265) / (2.0 * 3.14159265);
            t = frac(t + u_Angle / (2.0 * 3.14159265));
            break;
        }

        case 3: {
            // Diamond gradient
            float2 centered = abs(uv - u_Center);
            t = (centered.x + centered.y) * 2.0;
            break;
        }

        default:
            t = uv.x;
            break;
    }

    // Apply scale and offset
    t = t * u_Scale + u_Offset;

    // Apply repeat mode
    t = applyRepeat(t, u_Repeat);

    return sampleGradient(t);
}
