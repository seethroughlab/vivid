// Display Modes Example
// Demonstrates the different display scaling modes for output rendering.
// Press 1-5 to switch between modes, resize the window to see the effect.
//
// Keys:
//   1 - Stretch: Fill window, ignore aspect ratio (may distort)
//   2 - Fit: Maintain aspect ratio, letterbox/pillarbox as needed
//   3 - Fill: Maintain aspect ratio, crop to fill window
//   4 - FillHorizontal: Fill width, may crop top/bottom
//   5 - FillVertical: Fill height, may crop left/right

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

// Current mode name for display
static const char* modeNames[] = {
    "Stretch",
    "Fit",
    "Fill",
    "FillHorizontal",
    "FillVertical"
};

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Use a non-16:9 render resolution to make the effect obvious
    // 4:3 aspect ratio content in a 16:9 window
    ctx.setRenderResolution(800, 600);

    // Create a simple scene with clear aspect ratio indicators
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(800, 600);

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& canvas = chain.get<Canvas>("canvas");

    // Handle keyboard input to change display mode
    if (ctx.key(GLFW_KEY_1).pressed) ctx.displayMode(DisplayMode::Stretch);
    if (ctx.key(GLFW_KEY_2).pressed) ctx.displayMode(DisplayMode::Fit);
    if (ctx.key(GLFW_KEY_3).pressed) ctx.displayMode(DisplayMode::Fill);
    if (ctx.key(GLFW_KEY_4).pressed) ctx.displayMode(DisplayMode::FillHorizontal);
    if (ctx.key(GLFW_KEY_5).pressed) ctx.displayMode(DisplayMode::FillVertical);

    // Clear canvas
    canvas.clear(0.15f, 0.15f, 0.2f, 1.0f);

    float w = 800.0f;
    float h = 600.0f;

    // Draw border to show exact content bounds
    canvas.strokeStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.lineWidth(4.0f);
    canvas.strokeRect(2, 2, w - 4, h - 4);

    // Draw corner markers
    float cornerSize = 50.0f;
    canvas.fillStyle(1.0f, 0.3f, 0.3f, 1.0f);  // Red
    canvas.fillRect(0, 0, cornerSize, cornerSize);  // Top-left
    canvas.fillStyle(0.3f, 1.0f, 0.3f, 1.0f);  // Green
    canvas.fillRect(w - cornerSize, 0, cornerSize, cornerSize);  // Top-right
    canvas.fillStyle(0.3f, 0.3f, 1.0f, 1.0f);  // Blue
    canvas.fillRect(0, h - cornerSize, cornerSize, cornerSize);  // Bottom-left
    canvas.fillStyle(1.0f, 1.0f, 0.3f, 1.0f);  // Yellow
    canvas.fillRect(w - cornerSize, h - cornerSize, cornerSize, cornerSize);  // Bottom-right

    // Draw circle in center (to show distortion)
    canvas.fillStyle(0.5f, 0.5f, 0.8f, 1.0f);
    canvas.beginPath();
    canvas.arc(w / 2, h / 2, 150.0f, 0, 6.28318f);
    canvas.fill();

    // Draw crosshairs through center
    canvas.strokeStyle(1.0f, 1.0f, 1.0f, 0.5f);
    canvas.lineWidth(2.0f);
    canvas.beginPath();
    canvas.moveTo(0, h / 2);
    canvas.lineTo(w, h / 2);
    canvas.stroke();
    canvas.beginPath();
    canvas.moveTo(w / 2, 0);
    canvas.lineTo(w / 2, h);
    canvas.stroke();

    // Draw aspect ratio grid (4:3)
    canvas.strokeStyle(1.0f, 1.0f, 1.0f, 0.2f);
    canvas.lineWidth(1.0f);
    for (int i = 1; i < 4; i++) {
        float x = (w / 4) * i;
        canvas.beginPath();
        canvas.moveTo(x, 0);
        canvas.lineTo(x, h);
        canvas.stroke();
    }
    for (int i = 1; i < 3; i++) {
        float y = (h / 3) * i;
        canvas.beginPath();
        canvas.moveTo(0, y);
        canvas.lineTo(w, y);
        canvas.stroke();
    }

    // Draw current mode info
    int modeIdx = static_cast<int>(ctx.displayMode());
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(20, 20, 350, 100);

    // Mode name indicator (since Canvas doesn't have text, use shapes)
    // Draw mode indicator dots
    for (int i = 0; i < 5; i++) {
        float dotX = 40 + i * 30;
        float dotY = 50;
        if (i == modeIdx) {
            canvas.fillStyle(0.3f, 1.0f, 0.5f, 1.0f);
            canvas.beginPath();
            canvas.arc(dotX, dotY, 12.0f, 0, 6.28318f);
            canvas.fill();
        } else {
            canvas.strokeStyle(0.7f, 0.7f, 0.7f, 1.0f);
            canvas.lineWidth(2.0f);
            canvas.beginPath();
            canvas.arc(dotX, dotY, 10.0f, 0, 6.28318f);
            canvas.stroke();
        }
    }

    // Draw key hints as colored squares
    float keyY = 85;
    const char* keys[] = {"1", "2", "3", "4", "5"};
    for (int i = 0; i < 5; i++) {
        float keyX = 30 + i * 30;
        canvas.fillStyle(0.3f, 0.3f, 0.4f, 1.0f);
        canvas.fillRect(keyX - 8, keyY - 8, 16, 16);
        canvas.strokeStyle(0.6f, 0.6f, 0.7f, 1.0f);
        canvas.lineWidth(1.0f);
        canvas.strokeRect(keyX - 8, keyY - 8, 16, 16);
    }

    // Resolution indicator
    canvas.fillStyle(0.8f, 0.8f, 0.8f, 1.0f);
    canvas.fillRect(w - 120, h - 30, 100, 20);
    canvas.fillStyle(0.1f, 0.1f, 0.1f, 1.0f);
    // "800x600" text would go here if we had text rendering
}

// Start with 16:9 window (to contrast with 4:3 content)
VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1280,
    .windowHeight = 720,
    .displayMode = vivid::DisplayMode::Fit
}))
