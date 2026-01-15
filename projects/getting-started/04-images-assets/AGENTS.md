# Lesson 4: Images and Assets

## Commands
- Run: `vivid .`
- Show UI: `vivid . --show-ui` (press Tab to toggle)

## Modules
- Core: Image, HSV, Brightness, Blur, Bloom

## Lesson Focus
Image loading, asset organization, and image processing pipelines.

## Key Concepts
- **Image operator**: Loads image files as textures
- **Assets folder**: Convention for organizing project files
- **Relative paths**: Paths resolve relative to project directory
- **Hot reload**: Asset files reload when changed

## Suggested Modifications

1. **Use your own image**: Copy any .jpg or .png to assets/, update path in chain.cpp

2. **Create different filter styles**:
   - Vintage: `hsv.saturation = 0.7f; hsv.hueShift = 0.05f;`
   - High contrast B&W: `hsv.saturation = 0.0f; brightness.contrast = 1.5f;`
   - Dreamy glow: `blur.radius = 20.0f; bloom.intensity = 0.5f;`

3. **Add more effects**: Pixelate, Mirror, Posterize, ChromaticAberration

## Supported Image Formats
JPEG, PNG, BMP, TGA, HDR

## Troubleshooting
- **Black screen / no image**: Check the file path is correct
- **Image not updating**: Make sure you saved the file
- **Wrong aspect ratio**: Image is scaled to fill; use Transform to adjust

## Next
05-audio-reactive: Making visuals respond to sound
