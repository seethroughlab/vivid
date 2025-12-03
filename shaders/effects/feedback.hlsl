// Feedback shader - Recursive buffer with decay for motion trails

Texture2D g_Current;    // Current frame input
Texture2D g_Previous;   // Previous frame (feedback buffer)
SamplerState g_Sampler;

cbuffer Constants {
    float u_Decay;          // Feedback decay (0-1, lower = longer trails)
    float u_Mix;            // Mix between current and feedback (0-1)
    float2 u_Displace;      // Displacement per frame (for motion)
    float u_Zoom;           // Zoom per frame (1.0 = no zoom)
    float u_Rotate;         // Rotation per frame (radians)
    float2 u_Pivot;         // Transform pivot point
};

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float2 transformUV(float2 uv, float2 pivot, float zoom, float rotate, float2 displace) {
    // Move to pivot
    uv -= pivot;

    // Apply zoom
    uv /= zoom;

    // Apply rotation
    float c = cos(rotate);
    float s = sin(rotate);
    uv = float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);

    // Move back and apply displacement
    uv += pivot + displace;

    return uv;
}

float4 main(PSInput input) : SV_Target {
    float2 uv = input.uv;

    // Sample current frame
    float4 current = g_Current.Sample(g_Sampler, uv);

    // Transform UV for feedback sampling
    float2 feedbackUV = transformUV(uv, u_Pivot, u_Zoom, u_Rotate, u_Displace);

    // Sample previous frame with transformed UV
    float4 previous = g_Previous.Sample(g_Sampler, feedbackUV);

    // Apply decay to previous frame
    previous.rgb *= u_Decay;
    previous.a *= u_Decay;

    // Blend current and feedback
    float4 result;
    result.rgb = lerp(previous.rgb, current.rgb, u_Mix);
    result.a = max(current.a, previous.a);

    // Ensure we accumulate (take maximum with current)
    result.rgb = max(result.rgb, current.rgb * u_Mix);

    return result;
}
