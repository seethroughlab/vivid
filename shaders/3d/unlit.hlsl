// Basic unlit 3D shader with color and optional texture

cbuffer CameraConstants {
    column_major float4x4 u_ViewProjection;
    column_major float4x4 u_View;
    column_major float4x4 u_Projection;
    float4 u_CameraPosition;
};

cbuffer ModelConstants {
    column_major float4x4 u_Model;
    column_major float4x4 u_NormalMatrix;
};

cbuffer MaterialConstants {
    float4 u_Color;
    float u_UseTexture;
    float3 _padding;
};

Texture2D g_Texture;
SamplerState g_Sampler;

struct VSInput {
    float3 position : ATTRIB0;
    float3 normal : ATTRIB1;
    float2 uv : ATTRIB2;
    float4 tangent : ATTRIB3;
};

struct PSInput {
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

PSInput VSMain(VSInput input) {
    PSInput output;

    float4 worldPos = mul(u_Model, float4(input.position, 1.0));
    output.worldPos = worldPos.xyz;
    output.position = mul(u_ViewProjection, worldPos);
    output.normal = mul((float3x3)u_NormalMatrix, input.normal);
    output.uv = input.uv;

    return output;
}

float4 PSMain(PSInput input) : SV_Target {
    float4 color = u_Color;

    if (u_UseTexture > 0.5) {
        color *= g_Texture.Sample(g_Sampler, input.uv);
    }

    return color;
}
