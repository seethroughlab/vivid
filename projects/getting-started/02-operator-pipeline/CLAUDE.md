# Lesson 02: Operator Pipeline

This lesson teaches how operators connect to form processing pipelines.

## Lesson Objectives

1. Understand that operators form a directed graph
2. Learn both connection methods: `.input("name")` and `.input = op.output()`
3. Visualize the chain with Tab key
4. Understand that order affects the result

## Key Concepts

- **Pipeline**: Data flows from generators through effects to output
- **Input/Output**: Each operator can have inputs and produces output
- **Graph**: The chain is actually a directed graph, not just a linear chain
- **Order matters**: Blur→Colorize differs from Colorize→Blur

## What the Code Demonstrates

- Creating a three-stage pipeline: Noise → Blur → Colorize
- Two connection methods (string name vs direct reference)
- Setting effect parameters (blur radius, colorize gradient)

## Suggested Modifications

1. **Reorder effects**: Swap Blur and Colorize positions
   ```cpp
   colorize.input("noise");
   blur.input("colorize");
   chain.output("blur");
   ```

2. **Add parallel branches**:
   ```cpp
   auto& blur = chain.add<Blur>("blur");
   blur.input("noise");

   auto& edge = chain.add<Edge>("edge");
   edge.input("noise");  // Same input as blur!

   auto& comp = chain.add<Composite>("comp");
   comp.inputA("blur");
   comp.inputB("edge");
   comp.mode = BlendMode::Screen;
   ```

3. **Insert effects**:
   - `Pixelate` - blocky retro look
   - `Mirror` - symmetry effects
   - `ChromaticAberration` - RGB split
   - `Bloom` - glow effect

## Common Issues

- **Output shows wrong thing**: Check `chain.output()` points to the last operator
- **Effect not visible**: Make sure input is connected
- **Unexpected results**: Check operator order in the pipeline

## Next Lesson

03-parameters: Adding interactive sliders to control your chain
