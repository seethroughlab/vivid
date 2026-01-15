# Lesson 1: Hello Chain

## Commands
- Run: `vivid .`
- Show UI: `vivid . --show-ui` (press Tab to toggle)

## Modules
- Core: Noise, HSV

## Lesson Focus
The absolute basics of creating a Vivid chain with setup/update/VIVID_CHAIN pattern.

## Key Concepts
- **Context (ctx)**: Provides access to chain, time, input, window
- **Chain**: The container that holds operators
- **Operator**: A node that generates or processes textures
- **Hot reload**: Code changes apply instantly without restart

## Suggested Modifications

1. **Change noise parameters**:
   - `scale`: 1.0 (large blobs) to 20.0 (fine detail)
   - `speed`: 0.1 (slow) to 2.0 (fast)
   - `octaves`: 1-8 (more = richer detail, slower)

2. **Add color** by uncommenting the HSV section

3. **Try different noise types**:
   - `NoiseType::Simplex` (default), `NoiseType::Perlin`, `NoiseType::Worley`, `NoiseType::FBM`

4. **Use time for animation** in update():
   ```cpp
   auto& noise = ctx.chain().get<Noise>("noise");
   noise.scale = 4.0f + sin(ctx.time()) * 2.0f;
   ```

## Troubleshooting
- **Black screen**: Make sure `chain.output("noise")` is called
- **Compile error on save**: Check terminal for error message, fix and save again
- **Nothing changes**: Make sure you saved the file

## Next
02-operator-pipeline: Chaining multiple operators together
