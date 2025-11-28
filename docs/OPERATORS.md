# Built-in Operator Reference

This reference is auto-generated from the operator source files.

## Texture Operators (TOPs)

### Blur

Applies separable Gaussian blur to an input texture

**Parameters:**

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| radius | float | 5.0 | 0.0 - 50.0 |
| passes | int | 1 | 1 - 5 |

**Output:** Texture

**Fluent Methods:** `.radius()`, `.passes()`

---

### Brightness

Adjusts brightness and contrast of an input texture

**Parameters:**

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| amount | float | 1.0 | -1.0 - 2.0 |
| contrast | float | 1.0 | 0.0 - 3.0 |

**Output:** Texture

**Fluent Methods:** `.amount()`, `.amount()`, `.contrast()`

---

### Composite

Blends two textures using various blend modes

**Parameters:**

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| mode | int | 0 | 0 - 4 |
| mix | float | 0.5 | 0.0 - 1.0 |

**Output:** Texture

**Fluent Methods:** `.a()`, `.b()`, `.mode()`, `.mix()`

---

### Displacement

Distorts a texture using a displacement map

**Parameters:**

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| amount | float | 0.1 | 0.0 - 0.5 |
| channel | int | 0 | 0 - 3 |
| direction | vec2 | - | - |

**Output:** Texture

**Fluent Methods:** `.map()`, `.amount()`, `.channel()`, `.direction()`

---

### Edge

Sobel edge detection with multiple output modes

**Parameters:**

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| threshold | float | 0.1 | 0.0 - 1.0 |
| thickness | float | 1.0 | 0.5 - 5.0 |
| mode | int | 0 | 0 - 2 |

**Output:** Texture

**Fluent Methods:** `.threshold()`, `.thickness()`, `.mode()`

---

### Feedback

Creates trails/echo effects using double-buffered ping-pong

**Parameters:**

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| decay | float | 0.95 | 0.0 - 1.0 |
| zoom | float | 1.0 | 0.9 - 1.1 |
| rotate | float | 0.0 | -0.1 - 0.1 |
| translate | vec2 | - | - |

**Output:** Texture

**Fluent Methods:** `.decay()`, `.zoom()`, `.rotate()`, `.translate()`

---

### Gradient

Generates color gradients (linear, radial, angular, diamond)

**Parameters:**

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| mode | int | 0 | 0 - 3 |
| angle | float | 0.0 | 0.0 - 6.28318 |
| offset | float | 0.0 | 0.0 - 1.0 |
| scale | float | 1.0 | 0.1 - 10.0 |
| center | vec2 | - | - |

**Output:** Texture

**Fluent Methods:** `.mode()`, `.angle()`, `.offset()`, `.scale()`, `.center()`, `.color1()`, `.color2()`

---

### HSVAdjust

Adjusts hue, saturation, and value of an input texture

**Parameters:**

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| hueShift | float | 0.0 | -1.0 - 1.0 |
| saturation | float | 1.0 | 0.0 - 3.0 |
| value | float | 1.0 | 0.0 - 3.0 |

**Output:** Texture

**Fluent Methods:** `.hueShift()`, `.saturation()`, `.value()`

---

### Noise

Generates animated fractal noise texture

**Parameters:**

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| scale | float | 4.0 | 0.1 - 50.0 |
| speed | float | 1.0 | 0.0 - 10.0 |
| octaves | int | 4 | 1 - 8 |
| lacunarity | float | 2.0 | 1.0 - 4.0 |
| persistence | float | 0.5 | 0.0 - 1.0 |

**Output:** Texture

**Fluent Methods:** `.scale()`, `.speed()`, `.octaves()`, `.lacunarity()`, `.persistence()`

---

### Shape

Generates basic shapes using Signed Distance Fields (SDF)

**Parameters:**

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| center | vec2 | - | - |
| size | vec2 | - | - |
| radius | float | 0.2 | 0.0 - 1.0 |
| innerRadius | float | 0.1 | 0.0 - 1.0 |
| rotation | float | 0.0 | -3.14159 - 3.14159 |
| strokeWidth | float | 0.0 | 0.0 - 0.1 |
| color | color | - | - |
| softness | float | 0.005 | 0.001 - 0.1 |
| points | int | 5 | 3 - 12 |

**Output:** Texture

**Fluent Methods:** `.type()`, `.center()`, `.size()`, `.radius()`, `.innerRadius()`, `.rotation()`, `.strokeWidth()`, `.color()`, `.softness()`, `.points()`

---

### Transform

Applies translate, scale, and rotate to an input texture

**Parameters:**

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| translate | vec2 | - | - |
| scale | vec2 | - | - |
| rotate | float | 0.0 | -3.14159 - 3.14159 |
| pivot | vec2 | - | - |

**Output:** Texture

**Fluent Methods:** `.translate()`, `.scale()`, `.scale()`, `.rotate()`, `.pivot()`

---

## Channel Operators (CHOPs)

### LFO

Outputs a single oscillating value that can drive other parameters

**Parameters:**

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| freq | float | 1.0 | 0.01 - 100.0 |
| min | float | 0.0 | -1000.0 - 1000.0 |
| max | float | 1.0 | -1000.0 - 1000.0 |
| phase | float | - | 0.0 - 1.0 |
| waveform | int | 0 | 0 - 3 |

**Output:** Value

**Fluent Methods:** `.freq()`, `.min()`, `.max()`, `.phase()`, `.waveform()`

---

## Usage Example

```cpp
#include <vivid/vivid.h>

class MyPipeline : public vivid::Operator {
    vivid::Texture noise_, blurred_, output_;

public:
    void init(vivid::Context& ctx) override {
        noise_ = ctx.createTexture();
        blurred_ = ctx.createTexture();
        output_ = ctx.createTexture();
    }

    void process(vivid::Context& ctx) override {
        // Generate noise
        vivid::Context::ShaderParams noiseParams;
        noiseParams.param0 = 4.0f;  // scale
        noiseParams.param1 = ctx.time();
        ctx.runShader("shaders/noise.wgsl", nullptr, noise_, noiseParams);

        // Apply blur
        vivid::Context::ShaderParams blurParams;
        blurParams.param0 = 5.0f;  // radius
        ctx.runShader("shaders/blur.wgsl", &noise_, blurred_, blurParams);

        // Adjust brightness
        vivid::Context::ShaderParams brightParams;
        brightParams.param0 = 0.2f;  // brightness
        brightParams.param1 = 1.2f;  // contrast
        ctx.runShader("shaders/brightness.wgsl", &blurred_, output_, brightParams);

        ctx.setOutput("out", output_);
    }

    vivid::OutputKind outputKind() override { return vivid::OutputKind::Texture; }
};
VIVID_OPERATOR(MyPipeline)
```

