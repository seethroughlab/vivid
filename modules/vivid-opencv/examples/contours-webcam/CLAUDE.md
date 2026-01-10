# Contours - Webcam

Real-time contour detection from webcam using OpenCV.

## Features Demonstrated

- **Webcam** - Live camera input
- **Contours** - OpenCV edge detection and contour extraction
- **Canvas** - Side-by-side comparison view

## Key Concepts

### OpenCV Contour Detection

The Contours operator performs Canny edge detection followed by contour extraction:

```cpp
#include <vivid/opencv/opencv.h>

auto& contours = chain.add<vivid::opencv::Contours>("contours");
contours.input("cam");
contours.threshold1 = 50.0f;   // Canny low threshold
contours.threshold2 = 150.0f;  // Canny high threshold
contours.mode = 0;             // 0=External, 1=List, 2=CComp, 3=Tree
contours.lineWidth = 2.0f;
contours.colorG = 1.0f;        // Green contours
```

### Canny Thresholds

The two thresholds control edge detection sensitivity:

- **threshold1** (low): Edges below this are rejected
- **threshold2** (high): Edges above this are kept
- Edges between are kept if connected to strong edges

Typical values:
- Subtle edges: threshold1=30, threshold2=100
- Strong edges: threshold1=100, threshold2=200

### Contour Modes

| Mode | Value | Description |
|------|-------|-------------|
| External | 0 | Only outermost contours |
| List | 1 | All contours, no hierarchy |
| CComp | 2 | Two-level hierarchy |
| Tree | 3 | Full hierarchy tree |

## Controls

- **Mouse X**: Canny threshold 1 (0-255)
- **Mouse Y**: Canny threshold 2 (0-255)
- **1-4**: Select contour mode
- **+/-**: Adjust line width

## Performance Notes

Contour detection requires GPU→CPU→GPU data transfer. The operator only processes when the input changes, but with live webcam this means every frame. Expect some latency compared to pure GPU effects.
