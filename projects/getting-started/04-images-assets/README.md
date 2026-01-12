# Lesson 04: Images and Assets

Learn how to load images and organize project assets.

## What You'll Learn

- Loading images with the Image operator
- The `assets/` folder convention
- Processing images with effects
- Asset path resolution

## Prerequisites

- Completed Lesson 03: Parameters

## Run It

```bash
./build/bin/vivid projects/getting-started/04-images-assets
```

## Walkthrough

### The Assets Folder

Every Vivid project can have an `assets/` folder:

```
my-project/
├── chain.cpp
├── CLAUDE.md
└── assets/
    ├── photo.jpg
    ├── texture.png
    └── video.mp4
```

Paths in your code are relative to the project folder.

### Loading an Image

```cpp
auto& img = chain.add<Image>("img");
img.path = "assets/sample.jpg";
```

The Image operator loads the file and makes it available as a texture.

### Processing Images

Once loaded, you can apply any effect:

```cpp
auto& img = chain.add<Image>("img");
img.path = "assets/sample.jpg";

auto& blur = chain.add<Blur>("blur");
blur.input("img");
blur.radius = 10.0f;
```

### Hot Reload for Assets

If you replace an image file with the same name while running, Vivid will reload it automatically. Great for iterating on textures!

## Try It

1. **Add your own image**: Copy a .jpg or .png to the assets folder
2. **Change the path**: Update `img.path` to your file
3. **Add effects**: Try Pixelate, Mirror, or EdgeGlow
4. **Create a photo filter**: Combine HSV + Bloom for Instagram-style effects

## Example: Photo Filter Chain

```cpp
auto& img = chain.add<Image>("img");
img.path = "assets/photo.jpg";

auto& hsv = chain.add<HSV>("hsv");
hsv.input("img");
hsv.saturation = 1.3f;
hsv.value = 1.1f;

auto& bloom = chain.add<Bloom>("bloom");
bloom.input("hsv");
bloom.threshold = 0.7f;
bloom.intensity = 0.2f;

auto& vignette = chain.add<Vignette>("vignette");
vignette.input("bloom");
vignette.radius = 0.8f;
```

## Supported Formats

- **Images**: JPG, PNG, BMP, TGA, HDR
- **Videos**: See Lesson 06

## Asset Path Tips

| Path | Resolves To |
|------|-------------|
| `assets/photo.jpg` | `<project>/assets/photo.jpg` |
| `photo.jpg` | `<project>/photo.jpg` |
| `/absolute/path.jpg` | Exactly as specified |

## Next Steps

- **Lesson 05**: Make visuals react to audio
- **Explore**: `modules/vivid-core/examples/image-pipeline` for advanced processing
