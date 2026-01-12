# Lesson 01: Hello Chain

This is the first lesson in the Vivid Getting Started series. It teaches the absolute basics of creating a Vivid chain.

## Lesson Objectives

1. Understand the setup/update/VIVID_CHAIN pattern
2. Add a single operator (Noise)
3. Experience hot reload by editing while running
4. Learn basic keyboard controls

## Key Concepts

- **Context (ctx)**: Provides access to chain, time, input, window
- **Chain**: The container that holds operators
- **Operator**: A node that generates or processes textures
- **Hot reload**: Code changes apply instantly without restart

## What the Code Demonstrates

- Minimal working chain with one operator
- Setting operator properties (scale, speed)
- Specifying chain output
- Basic fullscreen toggle

## Suggested Modifications

When helping users experiment with this lesson:

1. **Change noise parameters**:
   - `scale`: 1.0 (large blobs) to 20.0 (fine detail)
   - `speed`: 0.1 (slow) to 2.0 (fast)
   - `octaves`: 1-8 (more = richer detail, slower)

2. **Add color** by uncommenting the HSV section

3. **Try different noise types**:
   - `NoiseType::Simplex` (default, organic)
   - `NoiseType::Perlin` (classic)
   - `NoiseType::Worley` (cellular)
   - `NoiseType::FBM` (fractal brownian motion)

4. **Use time for animation** in update():
   ```cpp
   auto& noise = ctx.chain().get<Noise>("noise");
   noise.scale = 4.0f + sin(ctx.time()) * 2.0f;
   ```

## Common Issues

- **Black screen**: Make sure `chain.output("noise")` is called
- **Compile error on save**: Check terminal for error message, fix and save again
- **Nothing changes**: Make sure you saved the file

## Next Lesson

02-operator-pipeline: Chaining multiple operators together
