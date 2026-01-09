# Image Pipeline

Demonstrates image loading and spatial transformation operators.

## Operators Used

- **Image** - Load image from file (PNG, JPG, BMP, TGA)
- **Transform** - Scale, rotate, translate
- **Mirror** - Horizontal/vertical flip and kaleidoscope
- **Tile** - Repeat texture in a grid
- **Noise** - Fallback if image not found
- **Canvas** - Grid layout

## Key Concepts

### Image Loading
```cpp
auto& image = chain.add<Image>("image");
image.file = "assets/images/photo.png";
```

### Transform Operations
```cpp
auto& transform = chain.add<Transform>("transform");
transform.input("source");
transform.scale.set(0.8f, 0.8f);      // Scale down
transform.rotation = 0.785f;           // Radians (45 degrees)
transform.translate.set(0.1f, 0.0f);   // Offset
```

### Mirror & Kaleidoscope
```cpp
auto& mirror = chain.add<Mirror>("mirror");
mirror.input("source");
mirror.mode = MirrorMode::Horizontal;   // Flip horizontally
mirror.mode = MirrorMode::Vertical;     // Flip vertically
mirror.mode = MirrorMode::Quad;         // Both axes (4 quadrants)
mirror.mode = MirrorMode::Kaleidoscope; // Kaleidoscope effect
mirror.segments = 6;                   // Number of kaleidoscope segments
mirror.angle = 0.0f;                   // Rotation offset
```

### Tiling
```cpp
auto& tile = chain.add<Tile>("tile");
tile.input("source");
tile.repeat.set(3.0f, 3.0f);  // 3x3 grid
tile.offset.set(0.5f, 0.0f);  // Offset each tile
```

## Fallback Pattern

If no image is found at the specified path, this example falls back to a colorful noise pattern. This demonstrates defensive coding for assets.

## Controls

No interactive controls - animations run automatically.
