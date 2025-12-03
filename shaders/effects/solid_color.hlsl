// Solid Color Shader - Vivid
// Fills the screen with a constant color

cbuffer Constants {
    float4 u_Color;  // RGBA color (0-1 range)
    float u_Time;    // Time in seconds
};

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    return u_Color;
}
