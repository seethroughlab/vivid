// Shape Operator Implementation - SDF-based shape generator

#include "vivid/operators/shape.h"
#include "vivid/context.h"
#include "vivid/shader_utils.h"

#include "Shader.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "MapHelper.hpp"

namespace vivid {

using namespace Diligent;

static const char* ShapePS_Source = R"(
cbuffer Constants : register(b0)
{
    float g_CenterX;
    float g_CenterY;
    float g_Radius;
    float g_InnerRadius;
    float g_Width;
    float g_Height;
    float g_Rotation;
    float g_Softness;
    float g_ColorR;
    float g_ColorG;
    float g_ColorB;
    int g_ShapeType;
    int g_Points;
    float g_AspectRatio;
    float g_BgColorR;
    float g_BgColorG;
    float g_BgColorB;
    float g_BgColorA;
    float _pad0;
    float _pad1;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static const float PI = 3.14159265359;

// SDF for circle
float sdCircle(float2 p, float r)
{
    return length(p) - r;
}

// SDF for box (rectangle)
float sdBox(float2 p, float2 b)
{
    float2 d = abs(p) - b;
    return length(max(d, float2(0.0, 0.0))) + min(max(d.x, d.y), 0.0);
}

// SDF for equilateral triangle
float sdTriangle(float2 p, float r)
{
    float k = sqrt(3.0);
    p.x = abs(p.x) - r;
    p.y = p.y + r / k;
    if (p.x + k * p.y > 0.0)
    {
        p = float2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
    }
    p.x -= clamp(p.x, -2.0 * r, 0.0);
    return -length(p) * sign(p.y);
}

// SDF for line segment
float sdLine(float2 p, float2 a, float2 b)
{
    float2 pa = p - a;
    float2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// SDF for ring (annulus)
float sdRing(float2 p, float outerR, float innerR)
{
    return abs(length(p) - (outerR + innerR) * 0.5) - (outerR - innerR) * 0.5;
}

// SDF for star
float sdStar(float2 p, float r, int n, float m)
{
    float an = PI / float(n);
    float en = PI / m;
    float2 acs = float2(cos(an), sin(an));
    float2 ecs = float2(cos(en), sin(en));

    float2 q = abs(p);
    float bn = fmod(atan2(q.x, q.y), 2.0 * an) - an;
    q = length(q) * float2(cos(bn), abs(sin(bn)));
    q = q - r * acs;
    q = q + ecs * clamp(-dot(q, ecs), 0.0, r * acs.y / ecs.y);
    return length(q) * sign(q.x);
}

// Rotate a 2D point
float2 rotate2D(float2 p, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

float4 main(in PSInput input) : SV_TARGET
{
    float2 center = float2(g_CenterX, g_CenterY);
    float3 fillColor = float3(g_ColorR, g_ColorG, g_ColorB);
    float4 bgColor = float4(g_BgColorR, g_BgColorG, g_BgColorB, g_BgColorA);
    float softness = max(g_Softness, 0.002);

    // Center UV and apply aspect ratio correction
    float2 uv = input.uv - center;
    if (g_AspectRatio > 0.0)
    {
        uv.x *= g_AspectRatio;
    }

    // Apply rotation
    if (g_Rotation != 0.0)
    {
        uv = rotate2D(uv, g_Rotation);
    }

    float d = 1.0;

    // Select shape type
    if (g_ShapeType == 0)
    {
        // Circle
        d = sdCircle(uv, g_Radius);
    }
    else if (g_ShapeType == 1)
    {
        // Rectangle
        float2 halfSize = float2(g_Width, g_Height) * 0.5;
        d = sdBox(uv, halfSize);
    }
    else if (g_ShapeType == 2)
    {
        // Triangle
        d = sdTriangle(uv, g_Radius);
    }
    else if (g_ShapeType == 3)
    {
        // Line
        float2 halfDir = float2(g_Width, g_Height) * 0.5;
        d = sdLine(uv, -halfDir, halfDir) - g_Radius;
    }
    else if (g_ShapeType == 4)
    {
        // Ring
        d = sdRing(uv, g_Radius, g_InnerRadius);
    }
    else if (g_ShapeType == 5)
    {
        // Star
        int numPoints = max(g_Points, 3);
        d = sdStar(uv, g_Radius, numPoints, 2.0);
    }

    // Smooth edge with antialiasing
    float alpha = 1.0 - smoothstep(-softness, softness, d);

    // Blend shape with background
    float4 shapeColor = float4(fillColor, alpha);
    return lerp(bgColor, shapeColor, alpha);
}
)";

void Shape::createPipeline(Context& ctx) {
    IShader* ps = ctx.shaderUtils().loadShaderFromSource(
        ShapePS_Source,
        "ShapePS",
        "main",
        SHADER_TYPE_PIXEL
    );

    if (!ps) return;

    pso_ = ctx.shaderUtils().createFullscreenPipeline("ShapePSO", ps, true);
    ps->Release();

    if (!pso_) return;

    createUniformBuffer(ctx, sizeof(Constants));
    pso_->CreateShaderResourceBinding(&srb_, true);

    if (srb_ && uniformBuffer_) {
        auto* cbVar = srb_->GetVariableByName(SHADER_TYPE_PIXEL, "Constants");
        if (cbVar) cbVar->Set(uniformBuffer_);
    }
}

void Shape::updateUniforms(Context& ctx) {
    if (!uniformBuffer_) return;

    float aspectRatio = static_cast<float>(ctx.width()) / static_cast<float>(ctx.height());

    MapHelper<Constants> cb(ctx.immediateContext(), uniformBuffer_, MAP_WRITE, MAP_FLAG_DISCARD);
    cb->centerX = centerX_;
    cb->centerY = centerY_;
    cb->radius = radius_;
    cb->innerRadius = innerRadius_;
    cb->width = width_;
    cb->height = height_;
    cb->rotation = rotation_;
    cb->softness = softness_;
    cb->colorR = color_.r;
    cb->colorG = color_.g;
    cb->colorB = color_.b;
    cb->shapeType = static_cast<int>(type_);
    cb->points = points_;
    cb->aspectRatio = aspectRatio;
    cb->bgColorR = bgColor_.r;
    cb->bgColorG = bgColor_.g;
    cb->bgColorB = bgColor_.b;
    cb->bgColorA = bgColor_.a;
}

void Shape::process(Context& ctx) {
    renderFullscreen(ctx);
}

} // namespace vivid
