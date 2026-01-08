# Parameter Modulation

Demonstrates lambda bindings for dynamic parameter control.

## Features Demonstrated

- **bind()** - Map a 0-1 normalized source to an output range
- **bindDirect()** - Direct value assignment from lambda
- **Mouse modulation** - Parameters controlled by mouse position
- **Time-based modulation** - Animated parameters using ctx.time()
- **LFO modulation** - Using LFO operator as modulation source

## Key Concepts

### Parameter Binding with Range Mapping

Use `bind()` when your source returns a normalized 0-1 value and you want to map it to a specific output range:

```cpp
// Source returns 0-1, output mapped to 5.0-20.0
noise.scale.bind(
    [&]() { return bands.bass(); },  // 0-1 normalized
    5.0f, 20.0f  // Output range
);

// Mouse X (-1 to 1) converted to 0-1, mapped to size range
shape.size.bind(
    [&]() {
        return (ctx.mouseNorm().x + 1.0f) * 0.5f;  // -1..1 -> 0..1
    },
    0.05f, 0.3f  // Size range
);
```

### Direct Parameter Binding

Use `bindDirect()` when your lambda returns the exact value you want:

```cpp
// Direct position control from LFO
shape.position.bindDirect(
    [&]() {
        float lfoVal = lfo.value();  // -1 to 1
        return glm::vec2(lfoVal * 0.3f, 0.0f);
    }
);

// Direct color control with time-based hue
shape.color.bindDirect(
    [&]() {
        float hue = std::fmod(ctx.time() * 0.2f, 1.0f);
        return hsvToRgb(hue, 1.0f, 1.0f);
    }
);
```

### Binding to Mouse Position

```cpp
// Size follows mouse X
shape.size.bind(
    [&]() { return (ctx.mouseNorm().x + 1.0f) * 0.5f; },
    minSize, maxSize
);

// Position follows mouse directly
shape.position.bindDirect(
    [&]() {
        glm::vec2 m = ctx.mouseNorm();
        return glm::vec2(m.x * 0.4f, m.y * 0.4f);
    }
);
```

### Binding to LFO

```cpp
auto& lfo = chain.add<LFO>("lfo");
lfo.frequency = 0.5f;
lfo.waveform = LFOWaveform::Sine;

// LFO outputs -1 to 1, convert to 0-1 for bind()
effect.param.bind(
    [&lfo]() { return (lfo.value() + 1.0f) * 0.5f; },
    minVal, maxVal
);
```

### Binding to Time

```cpp
// Oscillating value based on time
effect.intensity.bindDirect(
    [&ctx]() {
        return 0.5f + 0.3f * std::sin(ctx.time() * 2.0f);
    }
);

// Cycling through values
effect.mode.bindDirect(
    [&ctx]() {
        return static_cast<int>(ctx.time()) % 4;
    }
);
```

### Unbinding Parameters

```cpp
// Remove binding, return to manual control
shape.size.unbind();

// Check if bound
if (shape.size.isBound()) {
    // Parameter is being modulated
}
```

## Vec2 and Vec4 Parameter Bindings

Vector parameters support both uniform and per-component binding:

```cpp
// Uniform binding - both X and Y scale together
shape.size.bind(
    [&]() { return lfo.value(); },
    0.1f, 0.5f  // Both components use same range
);

// Direct binding for independent control
shape.size.bindDirect(
    [&]() {
        return glm::vec2(
            0.2f + std::sin(t) * 0.1f,
            0.3f + std::cos(t) * 0.1f
        );
    }
);
```

## Lambda Capture Patterns

### Capturing Context

```cpp
// Capture ctx by reference for time/mouse access
noise.scale.bindDirect([&ctx]() {
    return 5.0f + std::sin(ctx.time()) * 3.0f;
});
```

### Capturing Operators

```cpp
// Capture operator references for cross-modulation
shape.position.bindDirect([&lfo, &noise]() {
    return glm::vec2(lfo.value() * 0.3f, noise.value() * 0.2f);
});
```

### Using Global Pointers

For complex setups, store pointers globally:

```cpp
static Context* g_ctx = nullptr;
static LFO* g_lfo = nullptr;

void setup(Context& ctx) {
    g_ctx = &ctx;
    g_lfo = &chain.add<LFO>("lfo");

    shape.size.bind(
        []() { return (g_lfo->value() + 1.0f) * 0.5f; },
        0.1f, 0.4f
    );
}
```

## Common Modulation Patterns

### Audio-Reactive Parameters

```cpp
auto& fft = chain.add<FFT>("fft");
auto& bands = chain.add<BandSplit>("bands");
bands.input("fft");

// Size pulses with bass
shape.size.bind(
    [&bands]() { return bands.bass(); },
    0.1f, 0.5f
);

// Color intensity from mid frequencies
bloom.intensity.bind(
    [&bands]() { return bands.mid(); },
    0.5f, 2.0f
);
```

### Smooth Transitions

```cpp
static float smoothed = 0.0f;

shape.size.bindDirect([&]() {
    float target = (ctx.mouseNorm().x + 1.0f) * 0.5f;
    smoothed += (target - smoothed) * 0.1f;  // Lerp
    return smoothed * 0.3f + 0.05f;
});
```

## Controls

- **Move mouse horizontally**: Changes circle size (shape1)
- LFO automatically animates rectangle position (shape2)
- Time automatically cycles hexagon color (shape3)
- LFO automatically modulates noise scale
