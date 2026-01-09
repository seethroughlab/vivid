# Input Handling

Demonstrates mouse input, keyboard input, and modifier keys for interactive applications.

## Features Demonstrated

- **Mouse position** - Pixel and normalized coordinates
- **Mouse buttons** - Left, right, middle with press/hold/release states
- **Mouse delta** - Movement since last frame for drag interactions
- **Keyboard input** - Key state with press/hold/release
- **Modifier keys** - Shift, Ctrl, Alt, Super (Cmd on Mac)

## Key Concepts

### Mouse Position

```cpp
// Pixel coordinates (0,0 at top-left)
glm::vec2 mousePx = ctx.mouse();

// Normalized coordinates (0 to 1, Y axis down, origin at top-left)
glm::vec2 mouseNorm = ctx.mouseNorm();

// Movement since last frame
glm::vec2 delta = ctx.mouseDelta();      // Pixels
glm::vec2 deltaNorm = ctx.mouseDeltaNorm();  // Normalized
```

### Mouse Buttons

```cpp
// Button indices: 0=left, 1=right, 2=middle
auto leftBtn = ctx.mouseButton(0);
auto rightBtn = ctx.mouseButton(1);
auto middleBtn = ctx.mouseButton(2);

// Check state (these are bool fields, not methods)
if (leftBtn.pressed) { /* Just pressed this frame */ }
if (leftBtn.held) { /* Currently held down */ }
if (leftBtn.released) { /* Just released this frame */ }
```

### Keyboard Input

```cpp
#include <GLFW/glfw3.h>

// Check specific keys using GLFW key codes
auto spaceKey = ctx.key(GLFW_KEY_SPACE);
auto escKey = ctx.key(GLFW_KEY_ESCAPE);
auto aKey = ctx.key(GLFW_KEY_A);

// State checking (bool fields, not methods)
if (spaceKey.pressed) { /* Just pressed */ }
if (spaceKey.held) { /* Currently held */ }
if (spaceKey.released) { /* Just released */ }

// Common key codes:
// Letters: GLFW_KEY_A through GLFW_KEY_Z
// Numbers: GLFW_KEY_0 through GLFW_KEY_9
// Arrows: GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_LEFT, GLFW_KEY_RIGHT
// Special: GLFW_KEY_SPACE, GLFW_KEY_ENTER, GLFW_KEY_ESCAPE, GLFW_KEY_TAB
// Function: GLFW_KEY_F1 through GLFW_KEY_F12
```

### Modifier Keys

```cpp
// Check if modifier keys are held
bool shift = ctx.shiftHeld();   // Shift key
bool ctrl = ctx.ctrlHeld();     // Ctrl key
bool alt = ctx.altHeld();       // Alt/Option key
bool super = ctx.superHeld();   // Cmd (Mac) / Windows key

// Common pattern: speed boost with Shift
float speed = ctx.shiftHeld() ? 10.0f : 1.0f;
```

### Scroll Wheel

```cpp
// Get scroll delta (usually Y for vertical scroll)
glm::vec2 scroll = ctx.scroll();

if (scroll.y > 0) { /* Scrolled up */ }
if (scroll.y < 0) { /* Scrolled down */ }
```

## Common Patterns

### Drag Interaction

```cpp
static bool isDragging = false;
static glm::vec2 dragStart;

auto leftBtn = ctx.mouseButton(0);

if (leftBtn.pressed) {
    isDragging = true;
    dragStart = ctx.mouseNorm();
}

if (leftBtn.released) {
    isDragging = false;
}

if (isDragging && leftBtn.held) {
    glm::vec2 current = ctx.mouseNorm();
    glm::vec2 delta = current - dragStart;
    // Use delta for dragging...
}
```

### Toggle on Key Press

```cpp
static bool enabled = false;

if (ctx.key(GLFW_KEY_T).pressed) {
    enabled = !enabled;
}
```

### Continuous vs One-Shot Actions

```cpp
// One-shot: triggers once when pressed
if (ctx.key(GLFW_KEY_SPACE).pressed) {
    doAction();  // Fires once per press
}

// Continuous: triggers every frame while held
if (ctx.key(GLFW_KEY_UP).held) {
    position += speed * ctx.dt();  // Smooth movement
}
```

### Blocking Input for UI

```cpp
// When using ImGui or other UI, block input to prevent
// clicks on UI from affecting the scene
ctx.blockMouseInput();  // Call when mouse is over UI
```

## Controls

- **Left-drag**: Move the circle
- **Right-click**: Random color
- **Up/Down arrows**: Change size (hold Shift for faster)
- **Space**: Reset position
- **R**: Reset color
