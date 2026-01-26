# Lesson 01: Hello Chain

Your first Vivid project! This lesson introduces the core structure of a Vivid chain and the hot-reload workflow.

## What You'll Learn

- The `setup()` / `update()` / `VIVID_CHAIN` pattern
- Adding operators to create visuals
- Hot reload: edit code while running
- Basic window configuration

## Prerequisites

- Vivid built and ready (`cmake -B build && cmake --build build`)

## Run It

```bash
./build/bin/vivid projects/getting-started/01-hello-chain
```

You should see animated colorful noise filling the window.

## Walkthrough

### The Basic Structure

Every Vivid chain has three parts:

```cpp
void setup(Context& ctx) {
    // Called once when the chain loads
    // Add your operators here
}

void update(Context& ctx) {
    // Called every frame (~60fps)
    // Animate things, respond to input
}

VIVID_CHAIN(setup, update)  // Exports for the runtime
```

### Adding an Operator

In `setup()`, we add a **Noise** generator:

```cpp
auto& noise = ctx.chain().add<Noise>("noise");
noise.scale = 4.0f;   // Size of the noise pattern
noise.speed = 0.5f;   // How fast it animates
```

The `add<Type>("name")` pattern creates an operator and gives it a name. The name is used to reference it later.

### Setting the Output

```cpp
ctx.chain().output("noise");
```

This tells Vivid what to display. Without this, you'd see nothing!

### Hot Reload

While the program is running:
1. Edit `chain.cpp` (try changing `noise.scale = 8.0f`)
2. Save the file
3. Watch it update instantly!

If there's a compile error, check the terminal for details.

## Try It

1. **Change the scale**: Try values from 1.0 to 20.0
2. **Add color**: Uncomment the HSV section to add color shifting
3. **Change speed**: Make it faster (2.0) or slower (0.1)
4. **Press Tab**: Toggle the chain visualizer to see your node graph
5. **Press Cmd/Ctrl+F**: Toggle fullscreen

## Key Controls

| Key | Action |
|-----|--------|
| Tab | Toggle chain visualizer |
| Cmd/Ctrl+F | Toggle fullscreen |
| Esc | Quit |

## Next Steps

- **Lesson 02**: Chain multiple operators together
- **Module examples**: See `modules/vivid-core/examples/` for more generators

## Concepts Introduced

- **Context**: The runtime environment (`ctx`)
- **Chain**: Container for your operators (`ctx.chain()`)
- **Operator**: A processing node (Noise, Blur, etc.)
- **Hot reload**: Live code updates while running
