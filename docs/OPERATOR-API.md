# Vivid Operator API

> **Note:** Code examples show the target API. Implementation in progress with Diligent Engine.

This guide explains how to create custom operators for Vivid.

## Quick Start

A minimal operator that generates a texture:

```cpp
#include <vivid/vivid.h>

class MyOperator : public vivid::Operator {
    vivid::Texture output_;

public:
    void init(vivid::Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(vivid::Context& ctx) override {
        ctx.runShader("shaders/my_shader.wgsl", nullptr, output_);
        ctx.setOutput("out", output_);
    }

    vivid::OutputKind outputKind() const override {
        return vivid::OutputKind::Texture;
    }
};

VIVID_OPERATOR(MyOperator)
```

## Operator Lifecycle

### Required Methods

| Method | Description |
|--------|-------------|
| `process(Context& ctx)` | Called every frame. Generate your output here. |

### Optional Methods

| Method | Description |
|--------|-------------|
| `init(Context& ctx)` | Called once when the operator is created. Create textures here. |
| `cleanup()` | Called before the operator is destroyed. Release resources. |
| `saveState()` | Return state to preserve across hot-reload. |
| `loadState(state)` | Restore state after hot-reload. |
| `params()` | Return parameter declarations for the editor. |
| `outputKind()` | Return `Texture`, `Value`, `ValueArray`, or `Geometry`. |

## Context API

The `Context` object provides access to the runtime.

### Time and Frame Info

```cpp
float t = ctx.time();    // Seconds since start
float dt = ctx.dt();     // Delta time (seconds since last frame)
int f = ctx.frame();     // Frame number
```

### Resolution

```cpp
int w = ctx.width();     // Output width in pixels
int h = ctx.height();    // Output height in pixels
```

### Creating Textures

```cpp
// Create at default resolution
Texture tex = ctx.createTexture();

// Create at specific resolution
Texture tex = ctx.createTexture(512, 512);
```

### Running Shaders

```cpp
// Simple: no input texture
ctx.runShader("shaders/noise.wgsl", nullptr, output_);

// With input texture
ctx.runShader("shaders/blur.wgsl", &inputTex, output_);

// With parameters
Context::ShaderParams params;
params.param0 = 4.0f;   // scale
params.param1 = 0.5f;   // speed
params.mode = 1;        // mode selector
ctx.runShader("shaders/noise.wgsl", nullptr, output_, params);

// With two input textures
ctx.runShader("shaders/composite.wgsl", &tex1, &tex2, output_, params);

// With multiple input textures (up to 8)
std::vector<const Texture*> inputs = {&tex1, &tex2, &tex3, &tex4};
ctx.runShaderMulti("shaders/composite_multi.wgsl", inputs, output_, params);
```

### Setting Outputs

Every operator should set at least one output:

```cpp
// Texture output
ctx.setOutput("out", myTexture);

// Value output (single float)
ctx.setOutput("value", 0.73f);

// Value array output
ctx.setOutput("samples", std::vector<float>{0.1f, 0.5f, 0.9f});
```

### Getting Inputs from Other Operators

```cpp
// Get texture from another operator
Texture* input = ctx.getInputTexture("NoiseOp");

// Get value from another operator
float val = ctx.getInputValue("LFO", "out", 0.0f);  // default if not found
```

### 2D Instanced Rendering

For efficient rendering of many 2D shapes (like particles, physics simulations, data visualizations), use GPU instancing:

```cpp
#include <vivid/vivid.h>

void update(Chain& chain, Context& ctx) {
    // Create circle data
    std::vector<Circle2D> circles;

    for (int i = 0; i < 100; i++) {
        circles.emplace_back(
            glm::vec2(x, y),           // position (0-1 normalized)
            0.02f,                      // radius (normalized)
            glm::vec4(1, 0, 0, 1)      // color (RGBA)
        );
    }

    // Render all circles in ONE draw call
    Texture output = ctx.createTexture();
    ctx.drawCircles(circles, output, glm::vec4(0, 0, 0, 1));  // clearColor
    ctx.setOutput("out", output);
}
```

The `Circle2D` struct has multiple constructors:

```cpp
// Using glm types
Circle2D(glm::vec2 position, float radius, glm::vec4 color);

// Using individual floats
Circle2D(float x, float y, float radius, float r, float g, float b, float alpha = 1.0f);
```

**Performance:** All circles are rendered in a single GPU draw call using instancing, making it efficient for thousands of shapes.

**See:** `examples/2d-effects/particles/` for a complete particle system example.

## Shader Parameters

The `ShaderParams` struct maps to shader uniforms:

| C++ Field | Shader Uniform | Description |
|-----------|----------------|-------------|
| `param0` - `param7` | `u.param0` - `u.param7` | 8 generic float parameters |
| `vec0X`, `vec0Y` | `u.vec0` | 2D vector parameter |
| `vec1X`, `vec1Y` | `u.vec1` | 2D vector parameter |
| `mode` | `u.mode` | Integer mode selector |

## Hot-Reload State Preservation

To preserve state across hot-reload, implement `saveState()` and `loadState()`:

```cpp
struct MyState : vivid::OperatorState {
    float phase = 0.0f;
    int counter = 0;
};

class MyOperator : public vivid::Operator {
    float phase_ = 0.0f;
    int counter_ = 0;

    std::unique_ptr<OperatorState> saveState() override {
        auto state = std::make_unique<MyState>();
        state->phase = phase_;
        state->counter = counter_;
        return state;
    }

    void loadState(std::unique_ptr<OperatorState> state) override {
        if (auto* s = dynamic_cast<MyState*>(state.get())) {
            phase_ = s->phase;
            counter_ = s->counter;
        }
    }
};
```

## Parameter Declarations

Expose parameters for the editor by implementing `params()`:

```cpp
std::vector<ParamDecl> params() override {
    return {
        floatParam("scale", scale_, 0.1f, 50.0f),
        floatParam("speed", speed_, 0.0f, 10.0f),
        intParam("octaves", octaves_, 1, 8),
        boolParam("animate", animate_),
        vec2Param("offset", offset_),
        colorParam("tint", tint_)
    };
}
```

### Available Parameter Types

| Function | Type | Example |
|----------|------|---------|
| `floatParam(name, default, min, max)` | `float` | `floatParam("scale", 1.0f, 0.0f, 10.0f)` |
| `intParam(name, default, min, max)` | `int` | `intParam("count", 4, 1, 16)` |
| `boolParam(name, default)` | `bool` | `boolParam("enabled", true)` |
| `vec2Param(name, default, min, max)` | `glm::vec2` | `vec2Param("offset", vec2(0))` |
| `vec3Param(name, default, min, max)` | `glm::vec3` | `vec3Param("scale", vec3(1))` |
| `colorParam(name, default)` | `glm::vec3` | `colorParam("tint", vec3(1, 0.5, 0))` |
| `stringParam(name, default)` | `std::string` | `stringParam("label", "hello")` |

## Parameter Pattern (Chain API Compatible)

For operators to work with the Chain API, use public `Param<T>` members with direct assignment.

### Public Param Members

```cpp
class MyEffect : public TextureOperator {
public:
    // Public parameters - accessible via direct assignment
    Param<float> amount{"amount", 1.0f, 0.0f, 2.0f};  // name, default, min, max
    Param<float> intensity{"intensity", 0.5f, 0.0f, 1.0f};

    MyEffect() {
        // Register params for introspection (UI, serialization)
        registerParam(amount);
        registerParam(intensity);
    }

    // Input connection
    void input(TextureOperator* op) { setInput(0, op); }

    void process(Context& ctx) override {
        // Access params directly
        float amt = amount;  // implicit conversion
        float inten = intensity;
        // ... use values in shader
    }
};
```

### Usage in Chain API

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;      // Direct assignment
    noise.speed = 0.5f;
    noise.octaves = 4;

    auto& effect = chain.add<MyEffect>("effect");
    effect.input(&noise);    // Connection via setter
    effect.amount = 0.5f;    // Parameter via assignment
    effect.intensity = 0.8f;

    chain.output("effect");
}
```

### Generator Operators (No Input)

Generators don't need an `input()` method:

```cpp
class MyGenerator : public TextureOperator {
public:
    Param<float> scale{"scale", 4.0f, 0.1f, 20.0f};

    MyGenerator() {
        registerParam(scale);
    }

    void process(Context& ctx) override {
        // Generators create output from nothing
        float s = scale;
        // ... use in shader
    }
};
```

### Multiple Inputs

For operators that take multiple inputs (like Composite):

```cpp
class Blend : public TextureOperator {
public:
    Param<float> opacity{"opacity", 1.0f, 0.0f, 1.0f};

    Blend() {
        registerParam(opacity);
    }

    // Multiple input methods for connections
    void setInputA(TextureOperator* op) { setInput(0, op); }
    void setInputB(TextureOperator* op) { setInput(1, op); }
};
```

### Enum Parameters (Non-Param)

For enum-based modes, use void setters:

```cpp
class Noise : public TextureOperator {
public:
    Param<float> scale{"scale", 4.0f, 0.1f, 20.0f};

    // Enum mode uses void setter (not a Param)
    void setType(NoiseType t) {
        if (m_type != t) { m_type = t; markDirty(); }
    }

private:
    NoiseType m_type = NoiseType::Perlin;
};
```

## Output Types

Set the output type by overriding `outputKind()`:

```cpp
vivid::OutputKind outputKind() const override {
    return vivid::OutputKind::Texture;  // or Value, ValueArray, Geometry
}
```

| Type | Description | Preview |
|------|-------------|---------|
| `Texture` | 2D image | Thumbnail in preview panel |
| `Value` | Single float | Numeric display with sparkline |
| `ValueArray` | Float array | Waveform display |
| `Geometry` | 3D mesh (future) | Wireframe preview |

## Project Structure

A typical operator project:

```
my-project/
  CMakeLists.txt
  chain.cpp           # Your operators
  shaders/
    my_shader.wgsl    # Your shaders
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_operators)

set(VIVID_ROOT "/path/to/vivid" CACHE PATH "Path to Vivid")
include_directories(${VIVID_ROOT}/build/include)

add_library(operators SHARED chain.cpp)
set_target_properties(operators PROPERTIES PREFIX "lib")
```

## Complete Example

A fully-featured operator with all optional features:

```cpp
#include <vivid/vivid.h>

using namespace vivid;

struct PulseState : OperatorState {
    float phase = 0.0f;
};

class Pulse : public Operator {
    float frequency_ = 2.0f;
    float brightness_ = 1.0f;
    float phase_ = 0.0f;
    Texture output_;

public:
    // Setters
    void setFrequency(float f) { frequency_ = f; }
    void setBrightness(float b) { brightness_ = b; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        phase_ += ctx.dt() * frequency_;

        Context::ShaderParams params;
        params.param0 = phase_;
        params.param1 = brightness_;

        ctx.runShader("shaders/pulse.wgsl", nullptr, output_, params);
        ctx.setOutput("out", output_);

        // Also output the current value
        float value = (std::sin(phase_ * 6.28f) * 0.5f + 0.5f) * brightness_;
        ctx.setOutput("value", value);
    }

    std::unique_ptr<OperatorState> saveState() override {
        auto state = std::make_unique<PulseState>();
        state->phase = phase_;
        return state;
    }

    void loadState(std::unique_ptr<OperatorState> state) override {
        if (auto* s = dynamic_cast<PulseState*>(state.get())) {
            phase_ = s->phase;
        }
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("frequency", frequency_, 0.1f, 20.0f),
            floatParam("brightness", brightness_, 0.0f, 2.0f)
        };
    }

    OutputKind outputKind() const override {
        return OutputKind::Texture;
    }
};

VIVID_OPERATOR(Pulse)
```
