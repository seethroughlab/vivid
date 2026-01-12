# Lesson 02: Operator Pipeline

Learn how operators chain together to create complex effects from simple building blocks.

## What You'll Learn

- Connecting operators in sequence
- How data flows through the pipeline
- Using `.input()` to connect operators
- The chain visualizer graph

## Prerequisites

- Completed Lesson 01: Hello Chain

## Run It

```bash
./build/bin/vivid projects/getting-started/02-operator-pipeline
```

## Walkthrough

### The Pipeline Concept

In Vivid, you build effects by chaining operators together:

```
[Generator] → [Effect] → [Effect] → [Output]
```

Each operator receives input from the previous one and passes its output to the next.

### Connecting Operators

Use `.input()` with the operator name to connect operators:

```cpp
auto& blur = chain.add<Blur>("blur");
blur.input("noise");  // Connect to the "noise" operator
```

### The Chain in This Lesson

```
Noise → Blur → Lookup → Output
  │       │       │
  │       │       └── Colorizes using gradient LUT
  │       └── Softens the pattern
  └── Generates grayscale pattern
```

The `Lookup` operator uses the grayscale value as a coordinate to sample colors from a gradient texture. Dark values get the first color (dark blue), bright values get the second color (orange).

### Viewing the Graph

Press **Tab** to open the chain visualizer. You'll see your operators as nodes with connections between them. This is incredibly useful for understanding complex chains!

## Try It

1. **Change colors**: Modify the gradient's `colorA` and `colorB` values
2. **Add more effects**: Try adding `Pixelate` or `Mirror` after the colorize step
3. **Skip the blur**: Comment out the blur to see the raw noise colorized
4. **Change noise parameters**: Adjust `scale`, `speed`, and `octaves`

## Example: Adding Another Effect

```cpp
// After colorizing, add mirror symmetry
auto& mirror = chain.add<Mirror>("mirror");
mirror.input("colorize");
mirror.axis = MirrorAxis::Both;

chain.output("mirror");
```

## Key Insight

**Order matters!** Blur → Lookup produces different results than Lookup → Blur. The blur smooths the grayscale before colorizing, creating softer color transitions.

## Next Steps

- **Lesson 03**: Add interactive parameters with sliders
- **Explore**: `modules/vivid-core/examples/compositing` for blend modes
