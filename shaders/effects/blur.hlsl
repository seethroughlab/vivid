// Gaussian Blur - Separable implementation
// Use two passes: horizontal (u_Direction=0) then vertical (u_Direction=1)

Texture2D g_Texture;
SamplerState g_Sampler;

cbuffer Constants {
    float2 u_Resolution;    // Texture resolution
    float u_Radius;         // Blur radius in pixels
    int u_Direction;        // 0 = horizontal, 1 = vertical
    int u_Quality;          // Number of samples (higher = better quality)
    float _padding[3];
};

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Gaussian weight function
float gaussian(float x, float sigma) {
    return exp(-(x * x) / (2.0 * sigma * sigma));
}

float4 main(PSInput input) : SV_Target {
    float2 uv = input.uv;
    float2 texelSize = 1.0 / u_Resolution;

    // Direction vector (horizontal or vertical)
    float2 direction = (u_Direction == 0) ? float2(1.0, 0.0) : float2(0.0, 1.0);

    // Sigma based on radius (rule of thumb: sigma = radius / 3)
    float sigma = max(u_Radius / 3.0, 0.001);

    // Number of samples (clamped)
    int samples = clamp(u_Quality, 3, 64);
    int halfSamples = samples / 2;

    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float totalWeight = 0.0;

    // Sample in both directions from center
    for (int i = -halfSamples; i <= halfSamples; i++) {
        float offset = float(i) * (u_Radius / float(halfSamples));
        float weight = gaussian(offset, sigma);

        float2 sampleUV = uv + direction * offset * texelSize;
        color += g_Texture.Sample(g_Sampler, sampleUV) * weight;
        totalWeight += weight;
    }

    return color / totalWeight;
}
