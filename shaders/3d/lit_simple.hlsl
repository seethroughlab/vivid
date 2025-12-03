// Simple lit 3D shader with diffuse lighting

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
    float u_Ambient;
    float2 _padding;
};

cbuffer LightConstants {
    float4 u_LightDirection;  // xyz = direction, w = intensity
    float4 u_LightColor;
};

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
    output.normal = normalize(mul((float3x3)u_NormalMatrix, input.normal));
    output.uv = input.uv;

    return output;
}

float4 PSMain(PSInput input) : SV_Target {
    // Base color (no texture for now)
    float4 baseColor = u_Color;

    // Normalize interpolated normal
    float3 normal = normalize(input.normal);

    // Simple diffuse lighting
    float3 lightDir = normalize(-u_LightDirection.xyz);
    float NdotL = max(dot(normal, lightDir), 0.0);

    // Combine ambient and diffuse
    float3 ambient = baseColor.rgb * u_Ambient;
    float3 diffuse = baseColor.rgb * NdotL * u_LightColor.rgb * u_LightDirection.w;

    float3 finalColor = ambient + diffuse;

    return float4(finalColor, baseColor.a);
}
