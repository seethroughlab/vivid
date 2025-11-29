# Webcam Example

Demonstrates live camera capture using the built-in Webcam operator.

## Usage

```bash
./build/bin/vivid-runtime examples/webcam
```

## What You'll See

Live video feed from your default camera.

## Configuration

Edit `chain.cpp` to adjust camera settings:

```cpp
webcam_
    .device(0)              // Camera index (0 = first camera)
    .resolution(1280, 720)  // Capture resolution
    .frameRate(30.0f);      // Capture frame rate
```

## Available Cameras

When the example runs, it will print a list of available cameras:

```
[Webcam] Available cameras:
  [0] FaceTime HD Camera (default)
  [1] External USB Camera
```

Use `.device(index)` to select a specific camera.

## Combining with Effects

You can combine the webcam with other operators:

```cpp
// Add noise displacement to webcam
noise_.scale(4.0f).speed(0.3f);
// ... chain noise into displacement with webcam as source
```
