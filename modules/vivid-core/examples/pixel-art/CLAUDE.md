# Pixel Art

Creates pixel art aesthetics with chunky pixels and limited color palettes.

## Assets

- `assets/sprite.png` - Source image for pixel art effects

## Operators Used

- **Image** - Loads source image from assets
- **Pixelate** - Creates blocky mosaic pixels (different from Downsample)
- **Quantize** - Reduces colors per channel (posterization)
- **Canvas** - 2x2 comparison grid

## Key Concepts

### Pixelate Effect
Creates sharp-edged pixel blocks by sampling in grid cells:
```cpp
auto& pixelate = chain.add<Pixelate>("pixelate");
pixelate.input("source");
pixelate.size.set(16.0f, 16.0f);  // 16x16 pixel blocks
```

Parameters:
- `size` (vec2, 1-100, default 10,10) - Pixel block dimensions

### Quantize Effect
Reduces color depth by quantizing to discrete levels:
```cpp
auto& quantize = chain.add<Quantize>("quantize");
quantize.input("source");
quantize.levels = 4;  // 4 levels per channel = 64 total colors (4³)
```

Parameters:
- `levels` (int, 2-256, default 8) - Color levels per channel

### Combined Pixel Art Pipeline
True retro pixel art combines both effects:
```cpp
auto& pixelate = chain.add<Pixelate>("pixelate");
pixelate.input("source");
pixelate.size.set(8.0f, 8.0f);

auto& pixel_art = chain.add<Quantize>("pixel_art");
pixel_art.input("pixelate");  // Chain after pixelate
pixel_art.levels = 4;         // 64 colors
```

## Pixelate vs Downsample

| Pixelate | Downsample |
|----------|------------|
| Sharp pixel edges | Bilinear interpolation on upscale |
| Block sampling | Resolution reduction |
| Configurable block size | Target resolution |
| Better for pixel art | Better for CRT/retro monitor look |

## Controls

- **Mouse X** - Pixel block size (4-32)
- **Mouse Y** - Color quantization levels (2-16)

## Common Presets

### Game Boy (4 colors)
```cpp
pixelate.size.set(4.0f, 4.0f);
quantize.levels = 2;  // 2³ = 8 colors (or use 4 for true Game Boy)
```

### NES Style
```cpp
pixelate.size.set(3.0f, 3.0f);
quantize.levels = 4;  // 64 colors
```

### Early PC Graphics
```cpp
pixelate.size.set(2.0f, 2.0f);
quantize.levels = 16;  // 4096 colors
```

### Extreme Posterization
```cpp
quantize.levels = 2;  // 8 colors - very stylized
```
