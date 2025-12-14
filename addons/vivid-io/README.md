# vivid-io

Image loading utilities for LDR and HDR images using stb libraries.

## Installation

This addon is included with Vivid by default. No additional installation required.

## Functions

| Function | Description |
|----------|-------------|
| `loadImage` | Load LDR image (PNG, JPG, BMP, TGA) as RGBA |
| `loadImageFromMemory` | Load LDR image from memory buffer |
| `loadImageHDR` | Load HDR image (.hdr, .exr) as RGB floats |
| `loadImageHDRFromMemory` | Load HDR image from memory buffer |
| `fileExists` | Check if a file exists |
| `resolvePath` | Resolve path across search locations |

## Quick Start

```cpp
#include <vivid/io/image_loader.h>

// Load an LDR image
auto image = vivid::io::loadImage("assets/textures/diffuse.png");
if (image.valid()) {
    // Use image.pixels, image.width, image.height
}

// Load an HDR image
auto hdr = vivid::io::loadImageHDR("assets/hdris/environment.hdr");
if (hdr.valid()) {
    // Use hdr.pixels (RGB floats), hdr.width, hdr.height
}
```

## Supported Formats

**LDR (8-bit):** PNG, JPG, BMP, TGA, GIF, PSD, PIC, PNM

**HDR (32-bit float):** HDR (Radiance), EXR

## Dependencies

- vivid-core
- stb_image (bundled)

## License

MIT
