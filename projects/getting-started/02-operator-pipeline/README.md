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

There are two ways to connect operators:

**Method 1: Using `.input()` with the operator name**
```cpp
auto& blur = chain.add<Blur>("blur");
blur.input("noise");  // Connect to the "noise" operator
```

**Method 2: Using `.output()` from another operator**
```cpp
auto& blur = chain.add<Blur>("blur");
blur.input = noise.output();  // Direct connection
```

Both methods work - use whichever reads better for your chain.

### The Chain in This Lesson

```
Noise → Blur → Colorize → Output
  │       │        │
  │       │        └── Adds color gradient
  │       └── Softens the image
  └── Generates animated pattern
```

### Viewing the Graph

Press **Tab** to open the chain visualizer. You'll see your operators as nodes with connections between them. This is incredibly useful for understanding complex chains!

## Try It

1. **Reorder the chain**: Put Colorize before Blur - notice the difference?
2. **Add more effects**: Try adding `Pixelate` or `Mirror` between steps
3. **Create branches**: Use the same input for multiple effects, then composite them
4. **Remove a step**: Comment out the Blur to see the raw noise colorized

## Example: Adding Another Effect

```cpp
// After the blur, add edge detection
auto& edge = chain.add<Edge>("edge");
edge.input("blur");

// Then colorize the edges
auto& colorize = chain.add<Colorize>("colorize");
colorize.input("edge");
```

## Key Insight

**Order matters!** Blur → Colorize produces different results than Colorize → Blur. Experiment to understand how each effect transforms the image.

## Next Steps

- **Lesson 03**: Add interactive parameters with sliders
- **Explore**: `modules/vivid-core/examples/compositing` for blend modes
