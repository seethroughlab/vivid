# Window Control

Demonstrates window management, time functions, and display settings.

## Features Demonstrated

- **Window dimensions** - Width, height, aspect ratio
- **Window resize** - Programmatic and user-initiated
- **Fullscreen toggle** - Enter/exit fullscreen mode
- **VSync control** - Enable/disable vertical sync
- **Time functions** - Elapsed time, delta time, frame count

## Key Concepts

### Window Dimensions

```cpp
int w = ctx.width();     // Window width in pixels
int h = ctx.height();    // Window height in pixels
float aspect = ctx.aspect();  // width / height

// Check if window was resized this frame
if (ctx.wasResized()) {
    // Update any size-dependent resources
    canvas.size(ctx.width(), ctx.height());
}
```

### Window Management

```cpp
// Resize window programmatically
ctx.setWindowSize(1280, 720);

// Move window to position (screen coordinates)
ctx.setWindowPos(100, 100);

// Toggle fullscreen
ctx.fullscreen(true);   // Enter fullscreen
ctx.fullscreen(false);  // Exit fullscreen

// VSync control
ctx.vsync(true);   // Enable (smoother, capped at monitor refresh)
ctx.vsync(false);  // Disable (faster, may cause tearing)
```

### Time Functions

```cpp
// Elapsed time since start (seconds)
// Deterministic during recording mode
float t = ctx.time();

// Real wall-clock time (ignores recording mode)
float realT = ctx.realTime();

// Delta time - time since last frame (seconds)
// Deterministic during recording (1/fps)
float dt = ctx.dt();

// Real delta time - actual elapsed time
float realDt = ctx.realDt();

// Frame count (0-indexed)
uint64_t frame = ctx.frame();
```

### Time Usage Patterns

```cpp
// Smooth animation using delta time
position += velocity * ctx.dt();

// Time-based oscillation
float wave = std::sin(ctx.time() * 2.0f);

// Calculate FPS
float fps = 1.0f / ctx.dt();

// Frame-based logic (every N frames)
if (ctx.frame() % 60 == 0) {
    // Execute once per second at 60fps
}
```

### Recording Mode Behavior

During snapshot or recording mode:
- `ctx.time()` uses deterministic time steps (frame / fps)
- `ctx.dt()` returns constant value (1 / fps)
- `ctx.realTime()` and `ctx.realDt()` still use wall-clock time

This ensures reproducible output regardless of actual frame rate.

```cpp
// For animations that should be deterministic:
float angle = ctx.time() * rotationSpeed;  // Same in recording

// For UI/debug that should use real time:
float actualFps = 1.0f / ctx.realDt();  // Shows true FPS
```

## Common Patterns

### Responsive Layout

```cpp
void update(Context& ctx) {
    if (ctx.wasResized()) {
        // Recalculate layout based on new dimensions
        int cols = ctx.width() > 1200 ? 3 : 2;
        canvas.size(ctx.width(), ctx.height());
    }
}
```

### Fullscreen Toggle

```cpp
static bool isFullscreen = false;

if (ctx.key(GLFW_KEY_F11).pressed()) {
    isFullscreen = !isFullscreen;
    ctx.fullscreen(isFullscreen);
}
```

### Performance Mode

```cpp
// Disable VSync for maximum frame rate
if (ctx.key(GLFW_KEY_P).pressed()) {
    static bool perfMode = false;
    perfMode = !perfMode;
    ctx.vsync(!perfMode);
}
```

### Fixed Timestep Physics

```cpp
static float accumulator = 0.0f;
const float FIXED_DT = 1.0f / 60.0f;

accumulator += ctx.dt();

while (accumulator >= FIXED_DT) {
    // Physics update at fixed rate
    physicsStep(FIXED_DT);
    accumulator -= FIXED_DT;
}
```

## Controls

- **F**: Toggle fullscreen
- **V**: Toggle VSync
- **1**: Set window to 800x600
- **2**: Set window to 1280x720
- **3**: Set window to 1920x1080
- **4**: Set window to 640x480
- **C**: Move window to position (100, 100)
- **ESC**: Exit
