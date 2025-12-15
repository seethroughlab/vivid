# Pipeline Builder - Refactoring Utilities

This document describes the refactoring utilities implemented to reduce code duplication in the effects system.

## Implemented Utilities

### 1. Quick Wins (gpu_common.h)

**File:** `core/include/vivid/effects/gpu_common.h`

#### Shared Vertex Shader
```cpp
std::string shader = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + myFragmentShader;
```

#### Cached Samplers
```cpp
m_sampler = gpu::getLinearClampSampler(ctx.device());   // Most common
m_sampler = gpu::getNearestClampSampler(ctx.device());  // Pixel-art/exact values
m_sampler = gpu::getLinearRepeatSampler(ctx.device());  // Tiling textures
```

#### Resource Cleanup Helpers
```cpp
void cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    // Don't release cached samplers - managed by gpu_common
    m_sampler = nullptr;
}
```

#### String View Helper
```cpp
wgslDesc.code = gpu::toStringView(shaderSource.c_str());
```

### 2. WGSL Shader Composition (gpu_common.h)

**Namespace:** `vivid::effects::gpu::wgsl`

Include common shader functions by concatenating with your shader:

```cpp
std::string shader = std::string(gpu::FULLSCREEN_VERTEX_SHADER) +
                     gpu::wgsl::CONSTANTS +           // PI, TAU, E, PHI, SQRT2
                     gpu::wgsl::COLOR_CONVERT +       // rgb2hsv, hsv2rgb
                     myFragmentShader;
```

Available shader libraries:
| Constant | Functions |
|----------|-----------|
| `CONSTANTS` | PI, TAU, E, PHI, SQRT2 |
| `COLOR_CONVERT` | rgb2hsv, hsv2rgb |
| `NOISE_FUNCTIONS` | hash21, hash22, hash31, valueNoise, fbm |
| `SIMPLEX_NOISE` | simplexNoise (3D) |
| `BLEND_MODES` | blendMultiply, blendScreen, blendOverlay, blendSoftLight, blendHardLight, blendColorDodge, blendColorBurn, blendDifference, blendExclusion, blendLinearDodge, blendLinearBurn |
| `UV_UTILS` | rotateUV, scaleUV, tileUV, mirrorUV, polarUV |
| `SDF_PRIMITIVES` | sdCircle, sdBox, sdRoundedBox, sdSegment, sdTriangle, sdStar |

### 3. Pipeline Builder (pipeline_builder.h)

**File:** `core/include/vivid/effects/pipeline_builder.h`

Fluent API for creating render pipelines with less boilerplate:

```cpp
#include <vivid/effects/pipeline_builder.h>

void MyEffect::createPipeline(Context& ctx) {
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(MyUniforms))
           .texture(1)
           .sampler(2);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Still need to create uniform buffer manually
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(MyUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);
}
```

**Builder Methods:**
- `shader(source)` - Set WGSL shader source
- `vertexEntry(name)` - Set vertex entry point (default: "vs_main")
- `fragmentEntry(name)` - Set fragment entry point (default: "fs_main")
- `colorTarget(format)` - Set output format (default: RGBA16Float)
- `colorTargetWithBlend(format)` - Output with alpha blending
- `uniform(binding, size)` - Add uniform buffer binding
- `texture(binding)` - Add texture binding
- `sampler(binding, filtering)` - Add sampler binding (filtering=true for linear)
- `storageBuffer(binding, size)` - Add storage buffer binding

**Pre-defined Layouts:**
```cpp
auto layout = gpu::BindGroupLayouts::uniformTextureSampler(device, sizeof(MyUniforms));
auto layout = gpu::BindGroupLayouts::uniformTwoTexturesSampler(device, sizeof(MyUniforms));
auto layout = gpu::BindGroupLayouts::uniformOnly(device, sizeof(MyUniforms));
```

### 4. CRTP Template Base Classes (simple_texture_effect.h)

**File:** `core/include/vivid/effects/simple_texture_effect.h`

For the simplest effects, use the template base class:

#### SimpleTextureEffect (single input)

```cpp
#include <vivid/effects/simple_texture_effect.h>

struct InvertUniforms {
    float intensity;
    float _pad[3];
};

class Invert : public SimpleTextureEffect<Invert, InvertUniforms> {
public:
    Param<float> intensity{"intensity", 1.0f, 0.0f, 1.0f};

    // Required: provide uniform values via CRTP
    InvertUniforms getUniforms() const {
        return {intensity, {0, 0, 0}};
    }

    std::string name() const override { return "Invert"; }

protected:
    // Required: provide fragment shader
    const char* fragmentShader() const override {
        return R"(
struct Uniforms { intensity: f32, _pad1: f32, _pad2: f32, _pad3: f32 };
@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);
    let inverted = 1.0 - color.rgb;
    return vec4f(mix(color.rgb, inverted, uniforms.intensity), color.a);
}
)";
    }
};
```

#### SimpleGeneratorEffect (no input)

```cpp
struct MyGenUniforms {
    float time;
    float _pad[3];
};

class MyGenerator : public SimpleGeneratorEffect<MyGenerator, MyGenUniforms> {
public:
    MyGenUniforms getUniforms() const {
        return {m_time, {0, 0, 0}};
    }

protected:
    const char* fragmentShader() const override {
        return R"(
struct Uniforms { time: f32, _pad1: f32, _pad2: f32, _pad3: f32 };
@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return vec4f(sin(uniforms.time), cos(uniforms.time), 0.5, 1.0);
}
)";
    }
};
```

## Applicability Guide

| Utility | Use When |
|---------|----------|
| `gpu_common.h` quick wins | All effects - always use these |
| `wgsl::*` shader libs | Effects using color conversion, noise, SDFs, etc. |
| `PipelineBuilder` | Any effect - reduces pipeline boilerplate |
| `SimpleTextureEffect<>` | Single-input effects with uniform buffer |
| `SimpleGeneratorEffect<>` | Generator effects (no input) with uniform buffer |

**Not suitable for templates:**
- Multi-input effects (Composite, Displace) - use PipelineBuilder directly
- Multi-pass effects (Blur, Bloom) - use PipelineBuilder directly
- Effects with custom resource management (FrameCache, Particles)

## Files Reference

| File | Purpose |
|------|---------|
| `core/include/vivid/effects/gpu_common.h` | Vertex shader, samplers, release helpers, WGSL libs |
| `core/src/effects/gpu_common.cpp` | Sampler caching implementation |
| `core/include/vivid/effects/pipeline_builder.h` | Pipeline builder API |
| `core/src/effects/pipeline_builder.cpp` | Pipeline builder implementation |
| `core/include/vivid/effects/simple_texture_effect.h` | CRTP template base classes |

## Migration Examples

### Before (raw WebGPU)
```cpp
void createPipeline(Context& ctx) {
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource);
    // ... 40+ more lines
}
```

### After (with utilities)
```cpp
void createPipeline(Context& ctx) {
    std::string shader = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shader)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(MyUniforms))
           .texture(1)
           .sampler(2);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();
}
```

### Simplest (with template)
```cpp
// Just implement getUniforms() and fragmentShader()
// Everything else is handled by the template
class MyEffect : public SimpleTextureEffect<MyEffect, MyUniforms> {
    MyUniforms getUniforms() const { return {...}; }
    const char* fragmentShader() const override { return "..."; }
};
```
