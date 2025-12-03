// PBR Shader with Texture Map Support
// Implements metallic-roughness workflow with texture sampling

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
    float4 u_BaseColorFactor;   // Multiplied with albedo texture (16 bytes)
    float4 u_MaterialParams;    // x=metallic, y=roughness, z=aoStrength, w=normalScale (16 bytes)
    float4 u_Emissive;          // xyz = color, w = strength (16 bytes)
    float4 u_UseTextures;       // x=albedo, y=normal, z=metallic, w=roughness (16 bytes)
    float4 u_UseTextures2;      // x=ao, yzw=padding (16 bytes)
};

cbuffer LightConstants {
    float4 u_LightDirection;    // xyz = direction, w = intensity
    float4 u_LightColor;
    float4 u_AmbientColor;      // xyz = ambient color, w = intensity
};

// Texture samplers
Texture2D g_AlbedoMap;
Texture2D g_NormalMap;
Texture2D g_MetallicMap;
Texture2D g_RoughnessMap;
Texture2D g_AOMap;

SamplerState g_LinearSampler;

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
    float3 tangent : TEXCOORD3;
    float3 bitangent : TEXCOORD4;
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

// Smith's method for geometry
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

// Sample and decode normal from normal map
float3 GetNormalFromMap(float2 uv, float3 N, float3 T, float3 B) {
    float3 tangentNormal = g_NormalMap.Sample(g_LinearSampler, uv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= u_MaterialParams.w; // normalScale

    float3x3 TBN = float3x3(T, B, N);
    return normalize(mul(tangentNormal, TBN));
}

PSInput VSMain(VSInput input) {
    PSInput output;

    float4 worldPos = mul(u_Model, float4(input.position, 1.0));
    output.worldPos = worldPos.xyz;
    output.position = mul(u_ViewProjection, worldPos);
    output.uv = input.uv;

    // Transform normal and tangent to world space
    float3 N = normalize(mul((float3x3)u_NormalMatrix, input.normal));
    float3 T = normalize(mul((float3x3)u_Model, input.tangent.xyz));

    // Re-orthogonalize tangent with respect to normal
    T = normalize(T - dot(T, N) * N);

    // Calculate bitangent
    float3 B = cross(N, T) * input.tangent.w;

    output.normal = N;
    output.tangent = T;
    output.bitangent = B;

    return output;
}

float4 PSMain(PSInput input) : SV_Target {
    // Extract material params
    float metallicFactor = u_MaterialParams.x;
    float roughnessFactor = u_MaterialParams.y;
    float aoStrength = u_MaterialParams.z;
    float normalScale = u_MaterialParams.w;

    // Sample textures
    float3 albedo = u_BaseColorFactor.rgb;
    if (u_UseTextures.x > 0.5) {
        float4 albedoSample = g_AlbedoMap.Sample(g_LinearSampler, input.uv);
        albedo *= albedoSample.rgb;
    }

    float metallic = metallicFactor;
    if (u_UseTextures.z > 0.5) {
        metallic *= g_MetallicMap.Sample(g_LinearSampler, input.uv).r;
    }

    float roughness = roughnessFactor;
    if (u_UseTextures.w > 0.5) {
        roughness *= g_RoughnessMap.Sample(g_LinearSampler, input.uv).r;
    }
    roughness = max(roughness, 0.04); // Clamp to avoid artifacts

    float ao = 1.0;
    if (u_UseTextures2.x > 0.5) {
        ao = lerp(1.0, g_AOMap.Sample(g_LinearSampler, input.uv).r, aoStrength);
    }

    // Get normal (from map or interpolated)
    float3 N = normalize(input.normal);
    if (u_UseTextures.y > 0.5) {
        float3 tangentNormal = g_NormalMap.Sample(g_LinearSampler, input.uv).xyz * 2.0 - 1.0;
        tangentNormal.xy *= normalScale;
        float3x3 TBN = float3x3(normalize(input.tangent), normalize(input.bitangent), N);
        N = normalize(mul(tangentNormal, TBN));
    }

    float3 V = normalize(u_CameraPosition.xyz - input.worldPos);

    // Calculate reflectance at normal incidence (F0)
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
    kD *= 1.0 - metallic;

    // NdotL
    float NdotL = max(dot(N, L), 0.0);

    // Final outgoing light
    float3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // Ambient lighting with AO
    float3 ambient = u_AmbientColor.rgb * u_AmbientColor.w * albedo * ao;

    // Add emissive
    float3 emissive = u_Emissive.rgb * u_Emissive.w;

    float3 color = ambient + Lo + emissive;

    // HDR tonemapping (Reinhard)
    color = color / (color + float3(1.0, 1.0, 1.0));

    // Gamma correction
    color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));

    return float4(color, u_BaseColorFactor.a);
}
