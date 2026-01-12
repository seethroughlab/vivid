# Lesson 04: Images and Assets

This lesson teaches image loading and asset organization.

## Lesson Objectives

1. Load images using the Image operator
2. Understand the assets/ folder convention
3. Apply effects to loaded images
4. Create image processing pipelines

## Key Concepts

- **Image operator**: Loads image files as textures
- **Assets folder**: Convention for organizing project files
- **Relative paths**: Paths resolve relative to project directory
- **Hot reload**: Asset files reload when changed

## What the Code Demonstrates

- Loading an image from assets/
- Applying a multi-stage effect pipeline
- Creating a stylized photo filter effect

## Suggested Modifications

1. **Use your own image**:
   - Copy any .jpg or .png to the assets folder
   - Update the path in chain.cpp

2. **Create different filter styles**:
   ```cpp
   // Vintage look
   hsv.saturation = 0.7f;
   hsv.hueShift = 0.05f;
   ```

   ```cpp
   // High contrast B&W
   hsv.saturation = 0.0f;
   brightness.contrast = 1.5f;
   ```

   ```cpp
   // Dreamy glow
   blur.radius = 20.0f;
   bloom.intensity = 0.5f;
   ```

3. **Add more effects**:
   - `Pixelate` - retro pixel art
   - `Mirror` - symmetry
   - `Posterize` - reduce colors
   - `ChromaticAberration` - RGB split

## Common Issues

- **Black screen / no image**: Check the file path is correct
- **Image not updating**: Make sure you saved the file
- **Wrong aspect ratio**: Image is scaled to fill; use Transform to adjust

## Supported Image Formats

- JPEG (.jpg, .jpeg)
- PNG (.png)
- BMP (.bmp)
- TGA (.tga)
- HDR (.hdr) - for high dynamic range

## Next Lesson

05-audio-reactive: Making visuals respond to sound
