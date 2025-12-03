// PBR Shader with Cook-Torrance BRDF
// Implements metallic-roughness workflow

#define PI 3.14159265359

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

cbuffer PBRMaterialConstants {
    float4 u_BaseColor;
    float u_Metallic;
    float u_Roughness;
    float u_AO;           // Ambient occlusion
    float u_Padding1;
    float4 u_Emissive;    // xyz = color, w = strength
};

cbuffer LightConstants {
    float4 u_LightDirection;  // xyz = direction, w = intensity
    float4 u_LightColor;
    float4 u_AmbientColor;    // xyz = ambient color, w = ambient intensity
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

// Normal Distribution Function (GGX/Trowbridge-Reitz)
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / max(denom, 0.0001);
}

// Geometry Function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / max(denom, 0.0001);
}

// Smith's method for geometry (combines view and light)
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel-Schlick with roughness for IBL
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float3 oneMinusRoughness = float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness);
    return F0 + (max(oneMinusRoughness, F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

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
    // Material parameters
    float3 albedo = u_BaseColor.rgb;
    float metallic = u_Metallic;
    float roughness = max(u_Roughness, 0.04); // Clamp to avoid divide by zero
    float ao = u_AO;

    // Normalize interpolated normal
    float3 N = normalize(input.normal);
    float3 V = normalize(u_CameraPosition.xyz - input.worldPos);

    // Calculate reflectance at normal incidence (F0)
    // For dielectrics: 0.04, for metals: albedo
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);

    // Light direction (from surface to light)
    float3 L = normalize(-u_LightDirection.xyz);
    float3 H = normalize(V + L);

    // Light radiance
    float3 radiance = u_LightColor.rgb * u_LightDirection.w;

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    // Specular component
    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    float3 specular = numerator / denominator;

    // Energy conservation
    float3 kS = F;
    float3 kD = float3(1.0, 1.0, 1.0) - kS;
    kD *= 1.0 - metallic; // Metals have no diffuse

    // NdotL
    float NdotL = max(dot(N, L), 0.0);

    // Final outgoing light
    float3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // Ambient lighting (simplified, no IBL for now)
    float3 ambient = u_AmbientColor.rgb * u_AmbientColor.w * albedo * ao;

    // Add emissive
    float3 emissive = u_Emissive.rgb * u_Emissive.w;

    float3 color = ambient + Lo + emissive;

    // HDR tonemapping (Reinhard)
    color = color / (color + float3(1.0, 1.0, 1.0));

    // Gamma correction
    color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));

    return float4(color, u_BaseColor.a);
}
