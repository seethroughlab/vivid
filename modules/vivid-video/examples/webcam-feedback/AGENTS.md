# Webcam Feedback with Time Echo

Video feedback effect with delayed time echo. Combines instant spiraling trails
with a ghostly delayed version of yourself.

## Operators

| Operator | Purpose |
|----------|---------|
| Webcam | Live camera input |
| Feedback | Instant spiraling trails (1-frame delay) |
| FrameCache | Stores 45 frames of feedback history |
| TimeMachine | Samples delayed frame from cache |
| Composite | Blends feedback with delayed echo |

## Interaction

- **Mouse X** - Rotation speed (spiral direction)
- **Mouse Y** - Echo delay (5 to 40 frames behind)

## Key Parameters

### Feedback (instant trails)

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| decay | 0-1 | 0.96 | Trail persistence |
| mix | 0-1 | 0.2 | New frame vs feedback blend |
| zoom | 0.9-1.1 | 1.01 | Scale per frame |
| rotate | -0.1 to 0.1 | 0.015 | Rotation per frame |

### TimeMachine (delayed echo)

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| offset | 0-1 | 0.5 | Position in cache (0=newest, 1=oldest) |
| depth | 0-1 | 1.0 | How deep into cache to reach |

## Adding Time Delay to Feedback

This example shows the pattern for delayed feedback using existing operators:

```cpp
// 1. Core feedback (instant, no delay)
auto& feedback = chain.add<Feedback>("feedback");
feedback.input("source");

// 2. Cache the feedback output
auto& cache = chain.add<FrameCache>("cache");
cache.input("feedback");
cache.frameCount = 60;  // 2 seconds at 30fps

// 3. Sample delayed frame
auto& echo = chain.add<TimeMachine>("echo");
echo.cache(&cache);
echo.displacementMap(&solidWhite);  // Uniform delay
echo.offset = 0.5f;  // 50% into cache

// 4. Blend current with delayed
auto& blend = chain.add<Composite>("blend");
blend.inputA("feedback");
blend.inputB("echo");
blend.mode = BlendMode::Screen;
blend.opacity = 0.4f;
```

## Effect Recipes

### Tunnel with Echo
```cpp
feedback.zoom = 1.02f;
feedback.rotate = 0.0f;
echo.offset = 0.3f;  // Short delay
```

### Spiral with Ghost
```cpp
feedback.zoom = 1.005f;
feedback.rotate = 0.03f;
echo.offset = 0.7f;  // Long delay ghost
```

## Notes

- Requires webcam access
- Press D to see debug values
- Move mouse Y to control echo delay
- The echo adds a "ghost" that follows you with time lag
