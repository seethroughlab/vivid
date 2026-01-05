# vivid-opencv

OpenCV integration addon for Vivid.

> **Note:** This is a stub/template addon. It demonstrates the expected structure for community addons but does not yet contain a full implementation.

## Status

🚧 **Stub** - Structure only, no implementation yet.

## Planned Features

- `Webcam` operator using OpenCV capture
- `FaceDetect` operator for face detection
- `OpticalFlow` operator for motion detection
- `Contours` operator for edge/shape detection
- `ColorTrack` operator for color-based tracking

## Structure

This addon follows the standard Vivid addon structure:

```
vivid-opencv/
├── addon.json           # Addon metadata
├── CMakeLists.txt       # Build configuration
├── README.md            # This file
├── include/
│   └── vivid/
│       └── opencv/
│           └── opencv.h # Public headers
└── src/
    └── opencv.cpp       # Implementation
```

## Building

Requires OpenCV 4.x to be installed:

```bash
# macOS
brew install opencv

# Ubuntu
sudo apt install libopencv-dev
```

## Contributing

This addon is a template for community contributions. Feel free to implement the planned operators or add new ones!
