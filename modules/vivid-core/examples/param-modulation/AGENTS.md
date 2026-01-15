# Parameter Modulation

Demonstrates the different binding methods for dynamic parameter control in Vivid.

## Binding Methods Overview

| Method | Source | Trackable | Description |
|--------|--------|-----------|-------------|
| `bind(operator, inMin, inMax, outMin, outMax)` | Operator | Yes | Map operator output range to parameter range |
| `bind(lambda, outMin, outMax)` | Lambda | No | Map 0-1 lambda output to parameter range |
| `bindDirect(lambda)` | Lambda | No | Lambda returns exact value (full control) |
| `bindX()`/`bindY()` | Any | Depends | Per-component binding for Vec2/Vec3 |

**Trackable bindings** appear as dashed orange lines in the chain visualizer.

## This Example Shows

1. **Operator binding** (Circle): Position oscillates via LFO
2. **Lambda with range** (Square): Size controlled by mouse X
3. **Lambda direct** (Hexagon): Rotation controlled by time

## Key Concepts

### 1. Operator Binding (Trackable)

```cpp
auto& lfo = chain.add<LFO>("lfo");
lfo.frequency = 0.5f;

auto& shape = chain.add<Shape>("shape");
// LFO outputs -1 to 1, map to X position 0.2 to 0.8
shape.position.bindX(lfo, -1.0f, 1.0f, 0.2f, 0.8f);
//                   │     │      │     │     └─ output max
//                   │     │      │     └─ output min
//                   │     │      └─ input max (LFO range)
//                   │     └─ input min
//                   └─ source operator
```

This binding is **trackable** and appears as a dashed orange line in the chain visualizer.

### 2. Lambda with Range Mapping

```cpp
static Context* g_ctx = nullptr;  // Store context for lambdas

void setup(Context& ctx) {
    g_ctx = &ctx;

    auto& shape = chain.add<Shape>("shape");
    // Mouse X (0-1) maps to size 0.06 to 0.2
    shape.size.bind(
        [&]() { return g_ctx->mouseNorm().x; },  // Must return 0-1
        0.06f, 0.2f  // Output range
    );
}
```

Lambda must return a normalized 0-1 value. Not trackable (no visualizer line).

### 3. Lambda Direct (Full Control)

```cpp
auto& shape = chain.add<Shape>("shape");
// Rotation = time * 0.5 (exact radians, no mapping)
shape.rotation.bindDirect([&]() {
    return g_ctx->time() * 0.5f;  // Returns exact value
});
```

Lambda returns the exact parameter value. Use when you need full control over the calculation.

### 4. Per-Component Binding

```cpp
// Bind X and Y separately
shape.position.bindX(lfoX, -1.0f, 1.0f, 0.2f, 0.8f);
shape.position.bindY(lfoY, -1.0f, 1.0f, 0.3f, 0.7f);

// Or bind both uniformly
shape.size.bind(lfo, -1.0f, 1.0f, 0.1f, 0.3f);  // Both X and Y
```

## Parameter Types and Their Bindings

| Param Type | `bind()` | `bindDirect()` | Per-Component |
|------------|----------|----------------|---------------|
| `Param<float>` | Yes | Yes | N/A |
| `Vec2Param` | Yes (uniform) | No | `bindX()`, `bindY()` |
| `Vec3Param` | Yes (uniform) | No | `bindX()`, `bindY()`, `bindZ()` |
| `ColorParam` | No | No | `bindR()`, `bindG()`, `bindB()`, `bindA()` |

## Common Patterns

### Mouse Control
```cpp
// Size follows mouse X (0-1 → 0.1-0.4)
shape.size.bind(
    [&]() { return ctx.mouseNorm().x; },
    0.1f, 0.4f
);
```

### Time-Based Animation
```cpp
// Rotation spins at constant speed
shape.rotation.bindDirect([&]() {
    return ctx.time() * 0.5f;  // 0.5 rad/sec
});

// Oscillating value
effect.intensity.bindDirect([&]() {
    return 0.5f + 0.3f * std::sin(ctx.time() * 2.0f);
});
```

### LFO Modulation
```cpp
auto& lfo = chain.add<LFO>("lfo");
lfo.frequency = 0.5f;
lfo.waveform = LFOWaveform::Sine;  // -1 to 1 output

// Map LFO to parameter range
effect.param.bind(lfo, -1.0f, 1.0f, minVal, maxVal);
```

### Unbinding
```cpp
// Remove binding, return to manual control
shape.size.unbind();

// Check if bound
if (shape.size.isBound()) {
    // Parameter is being modulated
}
```

## Controls

- **Mouse X**: Changes square size (demonstrates lambda range binding)
- Circle oscillates automatically (LFO binding)
- Hexagon spins automatically (lambda direct binding)
