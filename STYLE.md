# Vivid Coding Style Guide

C++17 conventions for the Vivid codebase.

## Naming Conventions

### Members
```cpp
// Private/protected members: m_ prefix
class Foo {
private:
    int m_count = 0;
    std::string m_name;
    bool m_initialized = false;
};
```

### Types
```cpp
// Classes/structs: PascalCase
class TextureOperator { };
struct ParamDecl { };

// Enums: PascalCase for type, PascalCase for values
enum class NoiseType {
    Perlin,
    Simplex,
    Worley
};
```

### Functions and Variables
```cpp
// Functions: camelCase
void processFrame();
bool hasError() const;

// Local variables: camelCase
int frameCount = 0;
float deltaTime = ctx.dt();

// Constants: UPPER_SNAKE_CASE or constexpr camelCase
constexpr int MAX_PARTICLES = 10000;
constexpr float kDefaultScale = 1.0f;
```

### Parameters
```cpp
// Param<T> wrapper for UI-exposed parameters
Param<float> scale{"scale", 1.0f, 0.0f, 10.0f};  // name, default, min, max
```

## Operator Pattern

All operators inherit from `Operator` or `TextureOperator`:

```cpp
class MyEffect : public TextureOperator {
public:
    // Public parameters for direct access
    Param<float> amount{"amount", 1.0f, 0.0f, 2.0f};

    MyEffect() {
        registerParam(amount);
    }

    // Lifecycle
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "MyEffect"; }

private:
    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    bool m_initialized = false;
};
```

## Chain API

### Operator Access Pattern
Use name lookup, not global pointers (safe for hot-reload):

```cpp
// Good: Name lookup in update()
void setup(Context& ctx) {
    auto& noise = ctx.chain().add<Noise>("noise");
    noise.scale = 4.0f;
}

void update(Context& ctx) {
    auto& noise = ctx.chain().get<Noise>("noise");
    noise.speed = ctx.time();
}

// Bad: Global pointers (dangling after hot-reload)
Noise* g_noise = nullptr;  // Don't do this
```

### Input Connections
Operators take references for inputs, chain.output() takes string name:

```cpp
// Operator inputs: use references
blur.input(&noise);
comp.inputA(&feedback);
comp.inputB(&ramp);

// Chain output: use string name (for hot-reload safety)
chain.output("composite");
```

## Const Correctness

Methods that mutate state should not appear const:

```cpp
// Bad: Looks const but mutates
bool windowSizeChanged() {
    bool c = m_changed;
    m_changed = false;  // Mutation!
    return c;
}

// Good: Name indicates mutation
bool consumeWindowSizeChange() {
    bool c = m_changed;
    m_changed = false;
    return c;
}
```

## Virtual Methods

Always use `override` keyword:

```cpp
class Noise : public TextureOperator {
    void init(Context& ctx) override;      // override required
    void process(Context& ctx) override;
    std::string name() const override { return "Noise"; }
};
```

## Param<T> Casting

Explicit cast required when passing to std:: functions (template deduction):

```cpp
Param<float> m_scale{"scale", 1.0f, 0.0f, 10.0f};

// Wrong: Template deduction fails
float result = std::max(0.0f, m_scale);

// Right: Explicit cast
float result = std::max(0.0f, static_cast<float>(m_scale));
```

## GPU Resources

Initialize handles to nullptr, release in cleanup():

```cpp
class MyEffect : public TextureOperator {
    WGPUTexture m_texture = nullptr;
    WGPUBuffer m_buffer = nullptr;

    void cleanup() override {
        if (m_buffer) { wgpuBufferRelease(m_buffer); m_buffer = nullptr; }
        if (m_texture) { wgpuTextureRelease(m_texture); m_texture = nullptr; }
    }
};
```

## File Organization

```
core/
  include/vivid/           # Public headers
    effects/               # Effect operators
    network/               # Network operators
  src/                     # Implementation
    effects/               # Effect implementations

addons/
  vivid-audio/             # Audio addon
  vivid-video/             # Video addon
  vivid-render3d/          # 3D rendering addon
```

## Shader Code (WGSL)

```wgsl
// Struct definitions
struct Uniforms {
    time: f32,
    scale: f32,
    resolution: vec2f,
}

// Binding groups
@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;
@group(0) @binding(2) var inputSampler: sampler;

// Entry points
@vertex fn vs_main(@builtin(vertex_index) idx: u32) -> @builtin(position) vec4f { }
@fragment fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f { }
```

## Example Chain File

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.speed = 0.5f;

    auto& blur = chain.add<Blur>("blur");
    blur.input(&noise);
    blur.radius = 5.0f;

    chain.output("blur");
}

void update(Context& ctx) {
    auto& noise = ctx.chain().get<Noise>("noise");
    noise.offset.set(ctx.time(), 0, 0);
}

VIVID_CHAIN(setup, update)
```
