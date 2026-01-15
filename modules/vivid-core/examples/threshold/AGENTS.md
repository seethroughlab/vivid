# Threshold

Demonstrates the Threshold operator for binary thresholding of images and video.

## Operators Used

- **Threshold** - Binary threshold conversion
- **VideoPlayer** - Video input source
- **Canvas** - Grid layout display

## Key Concepts

### Basic Threshold
```cpp
auto& thresh = chain.add<Threshold>("thresh");
thresh.input("source");
thresh.threshold = 0.5f;  // Luminance cutoff (0-1)
thresh.softness = 0.0f;   // Hard edge
thresh.invert = 0.0f;     // Normal (bright = white)
```

### Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| threshold | 0-1 | 0.5 | Luminance cutoff point |
| softness | 0-1 | 0.0 | Edge blend width (0 = hard, 1 = very soft) |
| invert | 0-1 | 0.0 | Invert output (0 = normal, 1 = inverted) |

### Threshold Modes

**Hard Threshold (softness = 0):**
```cpp
thresh.threshold = 0.5f;
thresh.softness = 0.0f;
// Sharp black/white transition
```

**Soft Threshold (posterization effect):**
```cpp
thresh.threshold = 0.5f;
thresh.softness = 0.3f;
// Gradual transition creates posterized look
```

**Inverted Threshold:**
```cpp
thresh.threshold = 0.5f;
thresh.invert = 1.0f;
// Dark areas become white, bright areas become black
```

**Low Threshold (more white):**
```cpp
thresh.threshold = 0.3f;
// More of the image passes (appears whiter)
```

**High Threshold (more black):**
```cpp
thresh.threshold = 0.7f;
// Less of the image passes (appears darker)
```

## Common Use Cases

### Creating Masks
```cpp
// Use threshold output as mask for other effects
auto& thresh = chain.add<Threshold>("mask");
thresh.input("source");
thresh.threshold = 0.5f;

auto& composite = chain.add<Composite>("comp");
composite.inputA("background");
composite.inputB("foreground");
// Use threshold output to control visibility
```

### Silhouette Effect
```cpp
thresh.threshold = 0.3f;
thresh.invert = 1.0f;
// Creates dark silhouettes against bright background
```

### High Contrast Graphics
```cpp
thresh.threshold = 0.5f;
thresh.softness = 0.0f;
// Clean black and white for graphic design
```

### Animated Threshold
```cpp
// In update():
float t = ctx.time();
thresh.threshold = 0.5f + std::sin(t) * 0.3f;
// Threshold sweeps between 0.2 and 0.8
```

## Controls

No interactive controls - threshold animates automatically.

## Assets Required

Place a video file at `assets/video.mp4` in the example directory.

## TouchDesigner Equivalent

Threshold TOP
