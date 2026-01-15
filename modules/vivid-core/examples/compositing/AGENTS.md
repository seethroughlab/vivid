# Compositing

Demonstrates texture layering with blend modes and trackable value operator bindings.

## Operators Used

- **Composite** - Blend multiple textures with various modes
- **LFO** - Low frequency oscillator for modulation
- **Math** - Mathematical operations on values
- **Logic** - Comparison and boolean operations

## Value Binding Chain

This example demonstrates trackable value operator bindings that appear as
**dashed orange lines** in the chain visualizer:

```
LFO → Math.inputA → Logic.inputA → Composite.opacity
```

The bindings are set up in `setup()` and values flow automatically each frame.

## Key Concepts

### Composite Blend Modes
```cpp
auto& comp = chain.add<Composite>("comp");
comp.inputA("background");
comp.inputB("foreground");
comp.opacity = 1.0f;

// Blend modes
comp.mode = BlendMode::Over;       // Normal alpha compositing
comp.mode = BlendMode::Add;        // Additive (A + B)
comp.mode = BlendMode::Multiply;   // Darkens (A * B)
comp.mode = BlendMode::Screen;     // Lightens (1 - (1-A)(1-B))
comp.mode = BlendMode::Overlay;    // Combines multiply and screen
comp.mode = BlendMode::Difference; // Absolute difference |A - B|
```

### Multi-Layer Compositing
```cpp
// Chain multiple composites for layering
auto& comp1 = chain.add<Composite>("comp1");
comp1.inputA("background");
comp1.inputB("layer1");

auto& comp2 = chain.add<Composite>("comp2");
comp2.inputA("comp1");
comp2.inputB("layer2");
```

### Math Operations
```cpp
auto& math = chain.add<Math>("math");

// Basic arithmetic
math.operation(MathOperation::Add);       // A + B
math.operation(MathOperation::Subtract);  // A - B
math.operation(MathOperation::Multiply);  // A * B
math.operation(MathOperation::Divide);    // A / B (safe for B=0)

// Trigonometry
math.operation(MathOperation::Sin);       // sin(A)
math.operation(MathOperation::Cos);       // cos(A)

// Utility
math.operation(MathOperation::Abs);       // |A|
math.operation(MathOperation::Clamp);     // Clamp A to [minVal, maxVal]
math.operation(MathOperation::Pow);       // A^B
math.operation(MathOperation::Sqrt);      // sqrt(A)
math.operation(MathOperation::Floor);     // floor(A)
math.operation(MathOperation::Ceil);      // ceil(A)
math.operation(MathOperation::Fract);     // A - floor(A)
math.operation(MathOperation::Min);       // min(A, B)
math.operation(MathOperation::Max);       // max(A, B)

// Remapping (common for LFO output)
math.operation(MathOperation::Remap);
math.inputA = lfo.outputValue();  // -1 to 1
math.inMin = -1.0f;
math.inMax = 1.0f;
math.outMin = 0.0f;   // Remap to 0-1
math.outMax = 1.0f;

float result = math.value();
```

### Logic Operations
```cpp
auto& logic = chain.add<Logic>("logic");

// Comparison
logic.operation(LogicOperation::GreaterThan);     // A > B
logic.operation(LogicOperation::LessThan);        // A < B
logic.operation(LogicOperation::GreaterOrEqual);  // A >= B
logic.operation(LogicOperation::LessOrEqual);     // A <= B
logic.operation(LogicOperation::Equal);           // A == B (within epsilon)
logic.operation(LogicOperation::NotEqual);        // A != B

// Range check
logic.operation(LogicOperation::InRange);         // rangeMin <= A <= rangeMax
logic.rangeMin = 0.0f;
logic.rangeMax = 1.0f;

// Boolean operations (values > 0.5 are true)
logic.operation(LogicOperation::And);    // A && B
logic.operation(LogicOperation::Or);     // A || B
logic.operation(LogicOperation::Not);    // !A
logic.operation(LogicOperation::Toggle); // Flip-flop on trigger

// Usage
logic.inputA = someValue;
logic.inputB = threshold;
bool result = logic.result();
float floatResult = logic.outputValue();  // 0.0 or 1.0
```

## Common Compositing Workflows

### Glow Layer
```cpp
auto& bloom = chain.add<Bloom>("bloom");
bloom.input("source");

auto& comp = chain.add<Composite>("comp");
comp.inputA("source");
comp.inputB("bloom");
comp.mode = BlendMode::Add;
comp.opacity = 0.5f;
```

### Texture Overlay
```cpp
comp.inputA("photo");
comp.inputB("grainTexture");
comp.mode = BlendMode::Overlay;
comp.opacity = 0.3f;
```

### Conditional Blend (Non-Trackable)
```cpp
// Manual assignment - works but not visible in chain visualizer
logic.operation(LogicOperation::GreaterThan);
logic.inputA = lfo.outputValue();  // Direct assignment
logic.inputB = 0.0f;

comp.opacity = logic.result() ? 1.0f : 0.3f;  // Manual conditional
```

### Trackable Value Bindings (Recommended)
```cpp
// Trackable bindings - appear as dashed orange lines in chain visualizer
// LFO → Math → Logic → Composite.opacity

// LFO outputs -1 to 1
auto& lfo = chain.add<LFO>("lfo");
lfo.frequency = 0.5f;
lfo.waveform = LFOWaveform::Sine;

// Math remaps LFO output; bind LFO → Math.inputA
auto& math = chain.add<Math>("remap");
math.operation(MathOperation::Remap);
math.inMin = -1.0f; math.inMax = 1.0f;
math.outMin = 0.0f; math.outMax = 1.0f;
math.inputA.bindDirect(lfo);  // TRACKABLE: LFO → Math

// Logic compares; bind Math → Logic.inputA
auto& logic = chain.add<Logic>("compare");
logic.operation(LogicOperation::GreaterThan);
logic.inputB = 0.5f;
logic.inputA.bindDirect(math);  // TRACKABLE: Math → Logic

// Composite opacity; bind Logic → opacity with range mapping
// Logic outputs 0 or 1; map to opacity 0.3 or 1.0
comp.opacity.bind(logic, 0.0f, 1.0f, 0.3f, 1.0f);  // TRACKABLE: Logic → Composite
```

## Controls

No interactive controls - animations run automatically.
