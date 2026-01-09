// Input Handling Example
// Demonstrates: Mouse input, keyboard input, modifier keys
//
// Shows how to read user input and respond interactively

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <GLFW/glfw3.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

// State for interactive elements
static glm::vec2 circlePos = {0.5f, 0.5f};
static glm::vec4 circleColor = {1.0f, 0.4f, 0.2f, 1.0f};
static float circleSize = 0.1f;
static bool isDragging = false;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Background
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.1f, 0.1f, 0.15f, 1.0f);
    bg.setResolution(ctx.width(), ctx.height());

    // Interactive circle (position controlled by mouse)
    auto& circle = chain.add<Shape>("circle");
    circle.type = ShapeType::Circle;
    circle.size.set(circleSize, circleSize);
    circle.color.set(circleColor.r, circleColor.g, circleColor.b, circleColor.a);
    circle.softness = 0.01f;
    circle.setResolution(ctx.width(), ctx.height());

    // Composite
    auto& comp = chain.add<Composite>("comp");
    comp.inputA("bg");
    comp.inputB("circle");
    comp.mode = BlendMode::Over;
    comp.setResolution(ctx.width(), ctx.height());

    // Canvas for UI overlay
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "comp");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // MOUSE INPUT
    // =========================================================================

    // Get mouse position in normalized coordinates (0 to 1, Y down)
    glm::vec2 mouseNorm = ctx.mouseNorm();

    // Get mouse position in pixels (0,0 at top-left)
    glm::vec2 mousePx = ctx.mouse();

    // Get mouse delta (movement since last frame)
    glm::vec2 mouseDelta = ctx.mouseDeltaNorm();

    // Check mouse buttons: 0=left, 1=right, 2=middle
    // Each returns InputState with .pressed(), .held(), .released()
    auto leftBtn = ctx.mouseButton(0);
    auto rightBtn = ctx.mouseButton(1);
    auto middleBtn = ctx.mouseButton(2);

    // =========================================================================
    // KEYBOARD INPUT
    // =========================================================================

    // Check specific keys using GLFW key codes
    // Each returns InputState with .pressed(), .held(), .released()
    auto spaceKey = ctx.key(GLFW_KEY_SPACE);
    auto rKey = ctx.key(GLFW_KEY_R);
    auto upKey = ctx.key(GLFW_KEY_UP);
    auto downKey = ctx.key(GLFW_KEY_DOWN);
    auto escKey = ctx.key(GLFW_KEY_ESCAPE);

    // Check modifier keys (held state)
    bool shiftHeld = ctx.shiftHeld();
    bool ctrlHeld = ctx.ctrlHeld();
    bool altHeld = ctx.altHeld();

    // =========================================================================
    // INTERACTIVE BEHAVIOR
    // =========================================================================

    // Left-click drag to move circle
    if (leftBtn.pressed) {
        isDragging = true;
    }
    if (leftBtn.released) {
        isDragging = false;
    }
    if (isDragging && leftBtn.held) {
        // Both mouseNorm() and Shape use 0-1 range with Y-down
        circlePos = mouseNorm;
    }

    // Right-click to change color randomly
    if (rightBtn.pressed) {
        circleColor.r = 0.3f + static_cast<float>(rand()) / RAND_MAX * 0.7f;
        circleColor.g = 0.3f + static_cast<float>(rand()) / RAND_MAX * 0.7f;
        circleColor.b = 0.3f + static_cast<float>(rand()) / RAND_MAX * 0.7f;
    }

    // Space to reset position
    if (spaceKey.pressed) {
        circlePos = {0.5f, 0.5f};
    }

    // R to reset color
    if (rKey.pressed) {
        circleColor = {1.0f, 0.4f, 0.2f, 1.0f};
    }

    // Up/Down arrows to change size (faster with Shift)
    float sizeStep = shiftHeld ? 0.02f : 0.005f;
    if (upKey.held) {
        circleSize = std::min(0.5f, circleSize + sizeStep);
    }
    if (downKey.held) {
        circleSize = std::max(0.02f, circleSize - sizeStep);
    }

    // =========================================================================
    // APPLY STATE TO OPERATORS
    // =========================================================================

    auto& circle = chain.get<Shape>("circle");
    circle.position.set(circlePos.x, circlePos.y);  // Shape uses 0-1 range, (0.5,0.5) = center
    circle.size.set(circleSize, circleSize);
    circle.color.set(circleColor.r, circleColor.g, circleColor.b, circleColor.a);

    // =========================================================================
    // UI OVERLAY
    // =========================================================================

    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0, 0, 0, 0);

    // Draw the composite (background + circle) first
    auto& comp = chain.get<Composite>("comp");
    canvas.drawImage(comp, 0, 0, ctx.width(), ctx.height());

    // Info panel background
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(10, 10, 350, 200);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("Input Handling Demo", 20, 35);

    // Mouse info
    char mouseInfo[128];
    snprintf(mouseInfo, sizeof(mouseInfo), "Mouse: (%.0f, %.0f) px  |  (%.2f, %.2f) norm",
        mousePx.x, mousePx.y, mouseNorm.x, mouseNorm.y);
    canvas.fillText(mouseInfo, 20, 60);

    char btnInfo[64];
    snprintf(btnInfo, sizeof(btnInfo), "Buttons: L=%d M=%d R=%d",
        leftBtn.held ? 1 : 0, middleBtn.held ? 1 : 0, rightBtn.held ? 1 : 0);
    canvas.fillText(btnInfo, 20, 80);

    // Modifier keys
    char modInfo[64];
    snprintf(modInfo, sizeof(modInfo), "Modifiers: Shift=%d Ctrl=%d Alt=%d",
        shiftHeld ? 1 : 0, ctrlHeld ? 1 : 0, altHeld ? 1 : 0);
    canvas.fillText(modInfo, 20, 100);

    // Instructions
    canvas.fillStyle(0.7f, 0.9f, 1.0f, 1.0f);
    canvas.fillText("Controls:", 20, 130);
    canvas.fillStyle(0.8f, 0.8f, 0.8f, 1.0f);
    canvas.fillText("  Left-drag: Move circle", 20, 150);
    canvas.fillText("  Right-click: Random color", 20, 165);
    canvas.fillText("  Up/Down: Size (+Shift=fast)", 20, 180);
    canvas.fillText("  Space: Reset position  |  R: Reset color", 20, 195);
}

VIVID_CHAIN(setup, update)
