# Lesson 2: Operator Pipeline

## Commands
- Run: `vivid .`
- Show UI: `vivid . --show-ui` (press Tab to toggle)

## Modules
- Core: Noise, Blur, Gradient, Lookup

## Lesson Focus
How operators connect to form processing pipelines using `.input("name")`.

## Key Concepts
- **Pipeline**: Data flows from generators through effects to output
- **Input/Output**: Each operator can have inputs and produces output
- **Graph**: The chain is actually a directed graph, not just a linear chain
- **Order matters**: Blur->Lookup differs from Lookup->Blur

## Suggested Modifications

1. **Reorder effects**: Colorize before blur
   ```cpp
   colorize.input("noise");
   blur.input("colorize");
   chain.output("blur");
   ```

2. **Add parallel branches**:
   ```cpp
   auto& edge = chain.add<Edge>("edge");
   edge.input("noise");  // Same input as blur!
   auto& comp = chain.add<Composite>("comp");
   comp.inputA("blur");
   comp.inputB("edge");
   comp.mode = BlendMode::Screen;
   ```

3. **Insert effects**: Pixelate, Mirror, ChromaticAberration, Bloom

## Troubleshooting
- **Output shows wrong thing**: Check `chain.output()` points to the last operator
- **Effect not visible**: Make sure input is connected
- **Unexpected results**: Check operator order in the pipeline

## Next
03-parameters: Adding interactive sliders to control your chain
