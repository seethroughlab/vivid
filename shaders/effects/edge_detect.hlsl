// Edge Detection shader - Sobel and other edge detection filters

Texture2D g_Texture;
SamplerState g_Sampler;

cbuffer Constants {
    float2 u_Resolution;    // Texture resolution
    int u_Mode;             // 0=Sobel, 1=Prewitt, 2=Roberts, 3=Laplacian
    float u_Threshold;      // Edge threshold (0-1)
    float u_Strength;       // Edge strength multiplier
    int u_Invert;           // Invert output
    float3 u_EdgeColor;     // Edge color
    float3 u_BackgroundColor; // Background color
    float _padding;
};

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float luminance(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Sample luminance at offset
float sampleLum(float2 uv, float2 offset, float2 texelSize) {
    return luminance(g_Texture.Sample(g_Sampler, uv + offset * texelSize).rgb);
}

float4 main(PSInput input) : SV_Target {
    float2 uv = input.uv;
    float2 texelSize = 1.0 / u_Resolution;

    // Sample 3x3 neighborhood
    float tl = sampleLum(uv, float2(-1, -1), texelSize);
    float tm = sampleLum(uv, float2( 0, -1), texelSize);
    float tr = sampleLum(uv, float2( 1, -1), texelSize);
    float ml = sampleLum(uv, float2(-1,  0), texelSize);
    float mm = sampleLum(uv, float2( 0,  0), texelSize);
    float mr = sampleLum(uv, float2( 1,  0), texelSize);
    float bl = sampleLum(uv, float2(-1,  1), texelSize);
    float bm = sampleLum(uv, float2( 0,  1), texelSize);
    float br = sampleLum(uv, float2( 1,  1), texelSize);

    float gx, gy, edge;

    switch (u_Mode) {
        case 0: {
            // Sobel
            gx = (-1.0 * tl) + (-2.0 * ml) + (-1.0 * bl) + (1.0 * tr) + (2.0 * mr) + (1.0 * br);
            gy = (-1.0 * tl) + (-2.0 * tm) + (-1.0 * tr) + (1.0 * bl) + (2.0 * bm) + (1.0 * br);
            edge = sqrt(gx * gx + gy * gy);
            break;
        }

        case 1: {
            // Prewitt
            gx = (-1.0 * tl) + (-1.0 * ml) + (-1.0 * bl) + (1.0 * tr) + (1.0 * mr) + (1.0 * br);
            gy = (-1.0 * tl) + (-1.0 * tm) + (-1.0 * tr) + (1.0 * bl) + (1.0 * bm) + (1.0 * br);
            edge = sqrt(gx * gx + gy * gy);
            break;
        }

        case 2: {
            // Roberts Cross
            gx = mm - br;
            gy = mr - bm;
            edge = sqrt(gx * gx + gy * gy) * 1.414;
            break;
        }

        case 3: {
            // Laplacian
            edge = abs((-1.0 * tm) + (-1.0 * ml) + (4.0 * mm) + (-1.0 * mr) + (-1.0 * bm));
            break;
        }

        default:
            edge = 0.0;
            break;
    }

    // Apply strength
    edge *= u_Strength;

    // Apply threshold
    edge = edge > u_Threshold ? edge : 0.0;

    // Clamp
    edge = saturate(edge);

    // Invert if requested
    if (u_Invert != 0) {
        edge = 1.0 - edge;
    }

    // Apply colors
    float3 color = lerp(u_BackgroundColor, u_EdgeColor, edge);

    return float4(color, 1.0);
}
