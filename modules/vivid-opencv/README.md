# vivid-opencv

OpenCV computer vision integration for Vivid.

## Features

- **Contours** - Edge detection and contour drawing using Canny algorithm

### Planned
- OpticalFlow - Motion vector calculation
- ColorTrack - Color-based object tracking
- BlobTrack - Blob detection
- FaceDetect - Face detection

## Installation

The module automatically downloads [opencv-mobile](https://github.com/nihui/opencv-mobile) pre-built binaries during CMake configure. No manual installation required.

Supported platforms:
- macOS (arm64, x86_64)
- Linux (Ubuntu 22.04+)

### Windows (currently unavailable)

Windows builds are temporarily disabled due to MSVC STL ABI incompatibility with opencv-mobile v35.

**Error:** `LNK2019: unresolved external symbol __std_find_first_of_trivial_pos_1`

**Cause:** The opencv-mobile prebuilt libraries were compiled with MSVC 14.3x, which has breaking ABI changes compared to MSVC 14.43+ used in newer Visual Studio versions.

**Workarounds:**
1. Build OpenCV from source on Windows
2. Wait for opencv-mobile to release updated builds with newer MSVC

## Usage

```cpp
#include <vivid/vivid.h>
#include <vivid/opencv/opencv.h>

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Load an image
    auto& img = chain.add<Image>("img");
    img.file = "photo.jpg";

    // Detect contours
    auto& contours = chain.add<vivid::opencv::Contours>("contours");
    contours.input("img");
    contours.threshold1 = 50.0f;   // Canny threshold 1
    contours.threshold2 = 150.0f;  // Canny threshold 2
    contours.lineWidth = 2.0f;
    contours.colorG = 1.0f;        // Green contours

    chain.output("contours");
}
```

## Operator Reference

### Contours

Detects edges using Canny algorithm and extracts contours.

| Parameter | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| threshold1 | float | 0-255 | 100 | Canny first threshold |
| threshold2 | float | 0-255 | 200 | Canny second threshold |
| mode | int | 0-3 | 0 | Retrieval mode (0=External, 1=List, 2=CComp, 3=Tree) |
| lineWidth | float | 1-20 | 2 | Contour line thickness |
| colorR/G/B/A | float | 0-1 | 0,1,0,1 | Contour color (green default) |

## Examples

### contours-webcam
Real-time contour detection from webcam with side-by-side comparison.
```bash
./build/bin/vivid modules/vivid-opencv/examples/contours-webcam
```

**Controls:**
- Mouse X: Canny threshold 1 (0-255)
- Mouse Y: Canny threshold 2 (0-255)
- 1-4: Contour mode selection
- +/-: Line width

### contours-video
Contour detection on video files with overlay blending.
```bash
# Place your video as assets/video.mp4 first
./build/bin/vivid modules/vivid-opencv/examples/contours-video
```

## Architecture Notes

OpenCV operators require **CPU pixel data** from the input operator via the `cpuPixels()` interface. This avoids expensive GPU→CPU readback by using pixels that are already available on CPU before GPU upload.

**Compatible input sources:**
- `Webcam` - provides CPU pixels from camera capture
- `VideoPlayer` - provides CPU pixels from video decoding

**Incompatible sources:**
- GPU-only operators (shaders, effects) - these only have GPU textures
- To process GPU-generated content, you would need to capture to video first

This design ensures efficient CPU-based computer vision without redundant data transfers.
