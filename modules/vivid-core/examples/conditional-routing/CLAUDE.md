# Conditional Routing Example

Demonstrates conditional texture selection using Switch, Logic, and Math operators.

## Operators Demonstrated

- **Switch** - Selects between multiple texture inputs by index
- **Logic** - Performs logical/comparison operations
- **Math** - Performs mathematical operations

## Key Concepts

### Switch Operator
```cpp
chain.add<Switch>("selector");
auto& sw = chain.get<Switch>("selector");
sw.input(0, "texture_a");   // First option
sw.input(1, "texture_b");   // Second option
sw.input(2, "texture_c");   // Third option
sw.index = 1;               // Select second option
sw.blend = 0.2f;            // Soft crossfade duration
```

### Math Operations
```cpp
chain.add<Math>("calc").operation(MathOperation::Fract);
auto& math = chain.get<Math>("calc");
math.inputA = someValue;
math.inputB = anotherValue;  // For binary ops
float result = math.value(); // Get computed result
```

Available operations:
- `Add`, `Subtract`, `Multiply`, `Divide`
- `Fract` - Fractional part (sawtooth wave)
- `Floor`, `Ceil`, `Round`
- `Min`, `Max`, `Clamp`
- `Remap` - Map from one range to another
- `Abs`, `Sign`, `Negate`

### Logic Operations
```cpp
chain.add<Logic>("compare").operation(LogicOperation::LessThan);
auto& logic = chain.get<Logic>("compare");
logic.inputA = currentValue;
logic.inputB = threshold;
bool result = logic.result();
```

Available operations:
- `Equal`, `NotEqual`
- `GreaterThan`, `LessThan`
- `GreaterThanOrEqual`, `LessThanOrEqual`
- `InRange` - Check if value is within rangeMin/rangeMax
- `And`, `Or`, `Not`, `Xor`

## Pattern: Time-Based Selection

This example cycles through 3 options every 3 seconds:

```cpp
void update(Context& ctx) {
    float t = ctx.time();

    // Create 0-1 sawtooth wave with 3-second period
    auto& timeRemap = chain.get<Math>("time_remap");
    timeRemap.inputA = t / 3.0f;  // MathOperation::Fract gives 0-1

    // Scale to 0-3 for index selection
    auto& indexCalc = chain.get<Math>("index_calc");
    indexCalc.inputA = timeRemap.value();
    indexCalc.inputB = 3.0f;  // MathOperation::Multiply

    // Set switch index
    auto& sw = chain.get<Switch>("selector");
    sw.index = static_cast<int>(indexCalc.value());
}
```

## Related Operators

- **LFO** - Generate oscillating values for modulation
- **Ramp** - Linear interpolation over time
- **Composite** - Layer textures with blend modes
