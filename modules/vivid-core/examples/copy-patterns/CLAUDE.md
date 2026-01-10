# Copy Patterns

Creates geometric patterns by replicating shapes with per-copy transforms.

## Operators Used

- **Copy** - Replicates input with transforms (Linear, Radial, Grid modes)
- **Shape** - Source shapes (Circle, Triangle, Rectangle)
- **Bloom** - Post-process glow
- **Canvas** - Layout comparison

## Key Concepts

### Linear Mode
Creates trail/echo effects with offset, rotation, and scale:
```cpp
auto& linear = chain.add<Copy>("linear");
linear.input("shape");
linear.mode = CopyMode::Linear;
linear.count = 8;
linear.offset.set(0.06f, 0.0f);    // Per-copy offset
linear.rotationStep = 0.15f;        // Per-copy rotation (radians)
linear.scaleStep = 0.92f;           // Per-copy scale multiplier
linear.opacityFalloff = 0.12f;      // Per-copy opacity decay
```

Parameters:
- `count` (int, 1-16) - Number of copies
- `offset` (vec2) - Translation per copy
- `rotationStep` (float, radians) - Rotation increment
- `scaleStep` (float) - Scale multiplier (< 1 shrinks, > 1 grows)
- `opacityFalloff` (float, 0-1) - Opacity decay rate

### Radial Mode
Creates circular arrays like clock faces or flower petals:
```cpp
auto& radial = chain.add<Copy>("radial");
radial.input("shape");
radial.mode = CopyMode::Radial;
radial.count = 12;
radial.radius = 0.25f;              // Distance from center
radial.startAngle = 0.0f;           // Starting angle (radians)
radial.endAngle = 6.283f;           // Ending angle (2*PI = full circle)
```

Parameters:
- `radius` (float, 0-1) - Distance from center
- `startAngle` (float, radians) - First copy angle
- `endAngle` (float, radians) - Last copy angle

### Grid Mode
Creates uniform grids:
```cpp
auto& grid = chain.add<Copy>("grid");
grid.input("shape");
grid.mode = CopyMode::Grid;
grid.count = 16;
grid.columns = 4;                   // 4 columns = 4x4 grid
grid.spacing.set(0.12f, 0.12f);     // Cell spacing
```

Parameters:
- `columns` (int, 1-16) - Number of columns
- `spacing` (vec2) - Distance between copies

### Common Parameters
All modes support:
- `pivot` (vec2, 0-1) - Transform pivot point (default: center)
- `opacityFalloff` (float, 0-1) - Per-copy opacity decay

## Controls

- **Mouse X** - Linear offset distance / Radial copy count
- **Mouse Y** - Linear scale step

## Common Patterns

### Motion Trail
```cpp
copy.mode = CopyMode::Linear;
copy.count = 10;
copy.offset.set(0.05f, 0.0f);
copy.opacityFalloff = 0.15f;
```

### Spiral
```cpp
copy.mode = CopyMode::Linear;
copy.count = 16;
copy.offset.set(0.03f, 0.0f);
copy.rotationStep = 0.4f;
copy.scaleStep = 0.95f;
```

### Flower/Mandala
```cpp
copy.mode = CopyMode::Radial;
copy.count = 8;
copy.radius = 0.3f;
// Full circle by default
```

### Partial Arc
```cpp
copy.mode = CopyMode::Radial;
copy.count = 5;
copy.startAngle = -0.785f;  // -45 degrees
copy.endAngle = 0.785f;     // +45 degrees
```

### Centered Grid
```cpp
copy.mode = CopyMode::Grid;
copy.count = 9;
copy.columns = 3;
copy.spacing.set(0.2f, 0.2f);
// Grid is automatically centered
```

### Animated Radial
```cpp
// In update():
radial.startAngle = ctx.time() * 0.5f;
radial.endAngle = ctx.time() * 0.5f + 6.283f;
```

## Tips

- Use `opacityFalloff` for trail effects in Linear mode
- Radial mode automatically rotates copies to face outward
- Grid mode centers the entire grid on the shape position
- Combine with Bloom for glowing effects
- Max 16 copies per operator (shader limitation)
