# PLAN-12: Input & Interaction

Mouse, keyboard, gamepad, and touch input for interactive Vivid projects.

## Overview

Input handling for interactive visuals:

1. **Mouse Input** — Position, buttons, scroll, drag gestures
2. **Keyboard Input** — Key state, text input, shortcuts
3. **Gamepad Input** — Controllers, joysticks, buttons
4. **Touch Input** — Multi-touch for mobile/tablet

```
┌─────────────────────────────────────────────────────────────────┐
│                       INPUT SYSTEM                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Raw Input              Processed              Context API      │
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐  │
│  │ GLFW Events  │      │ Input State  │      │ ctx.mouse()  │  │
│  │ SDL Events   │─────▶│ Button Maps  │─────▶│ ctx.key()    │  │
│  │ OS Events    │      │ Axis Values  │      │ ctx.gamepad()│  │
│  └──────────────┘      └──────────────┘      └──────────────┘  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 12.13a: Mouse Input

### Context API

```cpp
// In update() function
void update(Chain& chain, Context& ctx) {
    // Position (normalized 0-1, origin top-left)
    glm::vec2 pos = ctx.mousePosition();

    // Position in pixels
    glm::vec2 pixelPos = ctx.mousePositionPixels();

    // Button state
    bool leftDown = ctx.isMouseButtonDown(MouseButton::Left);
    bool rightPressed = ctx.wasMouseButtonPressed(MouseButton::Right);
    bool middleReleased = ctx.wasMouseButtonReleased(MouseButton::Middle);

    // Scroll wheel
    glm::vec2 scroll = ctx.mouseScroll();  // Delta since last frame

    // Drag detection
    if (ctx.isMouseDragging(MouseButton::Left)) {
        glm::vec2 dragDelta = ctx.mouseDragDelta();
        glm::vec2 dragStart = ctx.mouseDragStart();
    }
}
```

### Mouse State Structure

```cpp
struct MouseState {
    glm::vec2 position;          // Normalized 0-1
    glm::vec2 positionPixels;    // Pixel coordinates
    glm::vec2 delta;             // Movement since last frame
    glm::vec2 scroll;            // Scroll delta

    bool buttons[5];             // Current state
    bool buttonsPressed[5];      // Just pressed this frame
    bool buttonsReleased[5];     // Just released this frame

    // Drag tracking per button
    bool dragging[5];
    glm::vec2 dragStart[5];
    glm::vec2 dragDelta[5];
};
```

### Implementation Tasks

- [x] Basic mouse position (already in Context)
- [ ] Mouse position normalized to 0-1
- [ ] Mouse button state tracking
- [ ] Press/release edge detection
- [ ] Scroll wheel support
- [ ] Drag gesture detection
- [ ] Cursor visibility control
- [ ] Cursor shape (arrow, hand, crosshair)

---

## Phase 12.13b: Keyboard Input

### Context API

```cpp
void update(Chain& chain, Context& ctx) {
    // Key state (already implemented)
    bool spaceDown = ctx.isKeyDown(Key::Space);
    bool enterPressed = ctx.wasKeyPressed(Key::Enter);
    bool escReleased = ctx.wasKeyReleased(Key::Escape);

    // Modifier keys
    bool shift = ctx.isKeyDown(Key::LeftShift) || ctx.isKeyDown(Key::RightShift);
    bool ctrl = ctx.isKeyDown(Key::LeftControl) || ctx.isKeyDown(Key::RightControl);

    // Text input (for UI fields)
    std::string text = ctx.textInput();  // Characters typed this frame

    // Shortcut detection
    if (ctx.wasKeyPressed(Key::S) && ctrl) {
        // Ctrl+S pressed
    }
}
```

### Key Codes

```cpp
enum class Key {
    // Letters
    A, B, C, ..., Z,

    // Numbers
    Num0, Num1, ..., Num9,

    // Function keys
    F1, F2, ..., F12,

    // Special keys
    Space, Enter, Escape, Tab, Backspace,
    Left, Right, Up, Down,
    Home, End, PageUp, PageDown,
    Insert, Delete,

    // Modifiers
    LeftShift, RightShift,
    LeftControl, RightControl,
    LeftAlt, RightAlt,
    LeftSuper, RightSuper,  // Windows/Cmd key
};
```

### Implementation Status

- [x] `isKeyDown(Key)` — Current key state
- [x] `wasKeyPressed(Key)` — Key just pressed
- [x] `wasKeyReleased(Key)` — Key just released
- [ ] Text input capture (for GUI text fields)
- [ ] Key repeat handling
- [ ] Modifier key helpers

---

## Phase 12.13c: Gamepad Input

### Context API

```cpp
void update(Chain& chain, Context& ctx) {
    // Check if gamepad connected
    if (!ctx.isGamepadConnected(0)) return;

    // Analog sticks (-1 to +1)
    glm::vec2 leftStick = ctx.gamepadAxis(0, GamepadAxis::LeftStick);
    glm::vec2 rightStick = ctx.gamepadAxis(0, GamepadAxis::RightStick);

    // Triggers (0 to 1)
    float leftTrigger = ctx.gamepadTrigger(0, GamepadTrigger::Left);
    float rightTrigger = ctx.gamepadTrigger(0, GamepadTrigger::Right);

    // Buttons
    bool aPressed = ctx.wasGamepadButtonPressed(0, GamepadButton::A);
    bool bDown = ctx.isGamepadButtonDown(0, GamepadButton::B);

    // D-pad
    bool dpadUp = ctx.isGamepadButtonDown(0, GamepadButton::DPadUp);
}
```

### Gamepad Mapping

Standard gamepad layout (Xbox/PlayStation style):

| Button | Xbox | PlayStation |
|--------|------|-------------|
| A | A | Cross |
| B | B | Circle |
| X | X | Square |
| Y | Y | Triangle |
| LB | Left Bumper | L1 |
| RB | Right Bumper | R1 |
| LT | Left Trigger | L2 |
| RT | Right Trigger | R2 |
| Start | Menu | Options |
| Select | View | Share |
| LS | Left Stick Click | L3 |
| RS | Right Stick Click | R3 |

### Implementation Tasks

- [ ] Gamepad enumeration
- [ ] Button state tracking
- [ ] Analog stick values
- [ ] Trigger values
- [ ] Dead zone handling
- [ ] Hot-plug detection
- [ ] Rumble/haptic feedback
- [ ] Gamepad database (SDL_GameControllerDB)

---

## Phase 12.13d: Touch Input

For mobile/tablet platforms and touch-enabled displays.

### Context API

```cpp
void update(Chain& chain, Context& ctx) {
    // Number of active touches
    int touchCount = ctx.touchCount();

    for (int i = 0; i < touchCount; i++) {
        Touch touch = ctx.getTouch(i);

        glm::vec2 pos = touch.position;     // Normalized 0-1
        int id = touch.id;                   // Unique touch ID
        TouchPhase phase = touch.phase;      // Began, Moved, Ended, Cancelled
    }

    // Gesture detection
    if (ctx.wasPinchGesture()) {
        float scale = ctx.pinchScale();
        glm::vec2 center = ctx.pinchCenter();
    }

    if (ctx.wasSwipeGesture()) {
        glm::vec2 direction = ctx.swipeDirection();
    }
}
```

### Touch Structure

```cpp
struct Touch {
    int id;              // Unique identifier for this touch
    glm::vec2 position;  // Normalized 0-1
    glm::vec2 delta;     // Movement since last frame
    TouchPhase phase;    // Began, Moved, Stationary, Ended, Cancelled
    float pressure;      // 0-1 if supported
    float radius;        // Touch radius if supported
};

enum class TouchPhase {
    Began,      // Just started
    Moved,      // Moving
    Stationary, // Touching but not moving
    Ended,      // Just lifted
    Cancelled   // Interrupted (phone call, etc.)
};
```

### Gesture Recognition

| Gesture | Detection |
|---------|-----------|
| Tap | Touch down + up within threshold |
| Double Tap | Two taps within time window |
| Long Press | Touch held for duration |
| Swipe | Quick movement in direction |
| Pinch | Two-finger scale change |
| Rotate | Two-finger rotation change |
| Pan | One or more finger drag |

### Implementation Tasks

- [ ] Touch event capture
- [ ] Multi-touch tracking
- [ ] Touch ID management
- [ ] Gesture recognizers
- [ ] Platform-specific backends (iOS, Android, Windows Touch)

---

## Input State Management

### Per-Frame State

```cpp
class InputManager {
public:
    void beginFrame();  // Called before processing events
    void endFrame();    // Called after update()

    // Mouse
    void onMouseMove(double x, double y);
    void onMouseButton(int button, bool pressed);
    void onMouseScroll(double x, double y);

    // Keyboard
    void onKey(int key, bool pressed);
    void onChar(unsigned int codepoint);

    // Gamepad
    void pollGamepads();

    // Access
    const MouseState& mouseState() const;
    const KeyboardState& keyboardState() const;
    const GamepadState& gamepadState(int index) const;

private:
    MouseState mouse_;
    MouseState mousePrevious_;
    KeyboardState keyboard_;
    KeyboardState keyboardPrevious_;
    GamepadState gamepads_[4];
};
```

### Edge Detection

```cpp
// Press detection: currently down AND was up last frame
bool wasPressed = current && !previous;

// Release detection: currently up AND was down last frame
bool wasReleased = !current && previous;
```

---

## Implementation Order

1. **Mouse Position** — Already implemented
2. **Mouse Buttons** — Click detection
3. **Keyboard** — Already implemented
4. **Mouse Scroll** — Zoom controls
5. **Mouse Drag** — Gesture detection
6. **Gamepad Basic** — Buttons and sticks
7. **Gamepad Advanced** — Triggers, rumble
8. **Touch Basic** — Single touch
9. **Touch Multi** — Multi-touch gestures

---

## Dependencies

| Library | Purpose | License |
|---------|---------|---------|
| GLFW | Window/input (current) | zlib |
| SDL2 | Alternative input backend | zlib |
| SDL_GameControllerDB | Gamepad mappings | Public Domain |

---

## References

- [GLFW Input Guide](https://www.glfw.org/docs/latest/input_guide.html)
- [SDL Game Controller](https://wiki.libsdl.org/CategoryGameController)
- [SDL_GameControllerDB](https://github.com/gabomdq/SDL_GameControllerDB)
