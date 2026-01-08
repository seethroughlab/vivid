# Tiling Patterns

Creates repeating patterns and kaleidoscopic effects.

## Operators Used

- **Tile** - Repeats texture in a grid
- **Transform** - Rotate/scale before tiling
- **Mirror** - Kaleidoscope symmetry
- **Bloom** - Post-process glow

## Key Concepts

### Basic Tiling
Repeat texture in a grid:
```cpp
auto& tile = chain.add<Tile>("tile");
tile.input("source");
tile.repeat.set(4.0f, 4.0f);   // 4x4 grid
tile.offset.set(0.0f, 0.0f);   // UV offset for scrolling
tile.mirror = false;           // Simple repeat
```

Parameters:
- `repeat` (vec2, 0.1-20) - Repetition count
- `offset` (vec2, -1 to 1) - UV offset for animation
- `mirror` (bool) - Mirror at tile boundaries

### Mirrored Tiling
Creates seamless patterns:
```cpp
tile.mirror = true;  // Each tile alternates flip direction
```

This eliminates seams where tiles meet by flipping alternating tiles.

### Transform Before Tiling
Rotate/scale creates complex patterns:
```cpp
auto& transform = chain.add<Transform>("transform");
transform.input("source");
transform.rotation = 0.785f;     // 45 degrees (radians)
transform.scale.set(0.7f, 0.7f); // Shrink

auto& tile = chain.add<Tile>("tile");
tile.input("transform");
tile.repeat.set(4.0f, 4.0f);
tile.mirror = true;
```

### Mirror/Kaleidoscope
Symmetrical reflections:
```cpp
auto& mirror = chain.add<Mirror>("mirror");
mirror.input("source");
mirror.mode = MirrorMode::Kaleidoscope;
mirror.segments = 8;  // 8-way symmetry
```

Mirror modes:
- `MirrorMode::Horizontal` - Left/right reflection
- `MirrorMode::Vertical` - Top/bottom reflection
- `MirrorMode::Quad` - 4-way symmetry
- `MirrorMode::Kaleidoscope` - N-segment radial symmetry

### Animated Kaleidoscope
Rotate source for hypnotic effects:
```cpp
auto& transform = chain.add<Transform>("transform");
transform.input("source");
transform.rotation = ctx.time() * 0.2f;

auto& kaleidoscope = chain.add<Mirror>("kaleidoscope");
kaleidoscope.input("transform");
kaleidoscope.mode = MirrorMode::Kaleidoscope;
kaleidoscope.segments = 8;
```

## Controls

- **Mouse X** - Tile repeat count (1-6)
- **Mouse Y** - Offset animation speed

## Common Patterns

### Wallpaper Pattern
```cpp
tile.repeat.set(4.0f, 4.0f);
tile.mirror = true;
```

### Infinite Zoom
Animate offset to scroll continuously:
```cpp
float scroll = std::fmod(ctx.time() * 0.1f, 1.0f);
tile.offset.set(scroll, scroll);
```

### Mandala
```cpp
auto& kaleidoscope = chain.add<Mirror>("kaleidoscope");
kaleidoscope.mode = MirrorMode::Kaleidoscope;
kaleidoscope.segments = 12;  // 12-fold symmetry
```

### Quilted Pattern
```cpp
transform.rotation = 0.785f;  // 45 degrees
transform.scale.set(0.5f, 0.5f);
tile.repeat.set(3.0f, 3.0f);
tile.mirror = true;
```
