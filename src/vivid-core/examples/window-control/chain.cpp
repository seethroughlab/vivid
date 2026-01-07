// Window Control Example
// Demonstrates: Window resize, fullscreen, vsync, time functions
//
// Shows how to control window properties and use time functions

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <GLFW/glfw3.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

// Preset window sizes
static const int PRESETS[][2] = {
    {800, 600},
    {1280, 720},
    {1920, 1080},
    {640, 480}
};
static int currentPreset = 1;
static bool vsyncEnabled = true;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Animated background using time
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 3.0f;
    noise.octaves = 3;

    // Canvas for UI overlay
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "noise");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // TIME FUNCTIONS
    // =========================================================================

    // Elapsed time since program start (seconds)
    // During recording mode, this uses deterministic time steps
    float t = ctx.time();

    // Real wall-clock time (ignores recording mode)
    float realT = ctx.realTime();

    // Delta time - time since last frame
    // Deterministic during recording (1/fps)
    float dt = ctx.dt();

    // Real delta time - actual wall-clock delta
    float realDt = ctx.realDt();

    // Current frame number (0-indexed)
    uint64_t frame = ctx.frame();

    // =========================================================================
    // WINDOW DIMENSIONS
    // =========================================================================

    int width = ctx.width();
    int height = ctx.height();
    float aspect = ctx.aspect();

    // Check if window was resized this frame
    bool wasResized = ctx.wasResized();

    // =========================================================================
    // KEYBOARD CONTROLS
    // =========================================================================

    // F: Toggle fullscreen
    if (ctx.key(GLFW_KEY_F).pressed) {
        static bool isFullscreen = false;
        isFullscreen = !isFullscreen;
        ctx.fullscreen(isFullscreen);
    }

    // V: Toggle VSync
    if (ctx.key(GLFW_KEY_V).pressed) {
        vsyncEnabled = !vsyncEnabled;
        ctx.vsync(vsyncEnabled);
    }

    // 1-4: Preset window sizes
    if (ctx.key(GLFW_KEY_1).pressed) {
        currentPreset = 0;
        ctx.setWindowSize(PRESETS[0][0], PRESETS[0][1]);
    }
    if (ctx.key(GLFW_KEY_2).pressed) {
        currentPreset = 1;
        ctx.setWindowSize(PRESETS[1][0], PRESETS[1][1]);
    }
    if (ctx.key(GLFW_KEY_3).pressed) {
        currentPreset = 2;
        ctx.setWindowSize(PRESETS[2][0], PRESETS[2][1]);
    }
    if (ctx.key(GLFW_KEY_4).pressed) {
        currentPreset = 3;
        ctx.setWindowSize(PRESETS[3][0], PRESETS[3][1]);
    }

    // C: Center window on screen
    if (ctx.key(GLFW_KEY_C).pressed) {
        // Position window at center of primary monitor
        // (approximate - doesn't account for taskbar)
        ctx.setWindowPos(100, 100);
    }

    // =========================================================================
    // HANDLE WINDOW RESIZE
    // =========================================================================

    // Update canvas size when window is resized
    if (wasResized) {
        auto& canvas = chain.get<Canvas>("canvas");
        canvas.size(width, height);
    }

    // =========================================================================
    // ANIMATE NOISE USING TIME
    // =========================================================================

    auto& noise = chain.get<Noise>("noise");
    noise.speed = 0.3f;
    // Use time for animation offset
    noise.offset.set(
        std::sin(t * 0.2f) * 2.0f,
        std::cos(t * 0.15f) * 2.0f,
        0.0f
    );

    // =========================================================================
    // UI OVERLAY
    // =========================================================================

    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0, 0, 0, 0);

    // Draw noise as background
    canvas.drawImage(noise, 0, 0, width, height);

    // Semi-transparent info panel
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.75f);
    canvas.fillRect(10, 10, 380, 280);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("Window Control Demo", 20, 35);

    // Window info
    char windowInfo[128];
    snprintf(windowInfo, sizeof(windowInfo), "Window: %d x %d  (aspect: %.3f)", width, height, aspect);
    canvas.fillText(windowInfo, 20, 60);

    if (wasResized) {
        canvas.fillStyle(1.0f, 1.0f, 0.0f, 1.0f);
        canvas.fillText("  [RESIZED THIS FRAME]", 20, 75);
        canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Time info
    char timeInfo[128];
    snprintf(timeInfo, sizeof(timeInfo), "Time: %.2fs  |  Real: %.2fs", t, realT);
    canvas.fillText(timeInfo, 20, 95);

    char dtInfo[128];
    snprintf(dtInfo, sizeof(dtInfo), "dt: %.4fs (%.1f fps)  |  realDt: %.4fs",
        dt, 1.0f / dt, realDt);
    canvas.fillText(dtInfo, 20, 115);

    char frameInfo[64];
    snprintf(frameInfo, sizeof(frameInfo), "Frame: %llu", frame);
    canvas.fillText(frameInfo, 20, 135);

    // VSync status
    char vsyncInfo[64];
    snprintf(vsyncInfo, sizeof(vsyncInfo), "VSync: %s", vsyncEnabled ? "ON" : "OFF");
    canvas.fillText(vsyncInfo, 20, 155);

    // Controls
    canvas.fillStyle(0.7f, 0.9f, 1.0f, 1.0f);
    canvas.fillText("Controls:", 20, 185);
    canvas.fillStyle(0.8f, 0.8f, 0.8f, 1.0f);
    canvas.fillText("  F: Toggle fullscreen", 20, 205);
    canvas.fillText("  V: Toggle VSync", 20, 220);
    canvas.fillText("  1: 800x600  |  2: 1280x720", 20, 235);
    canvas.fillText("  3: 1920x1080  |  4: 640x480", 20, 250);
    canvas.fillText("  C: Move window to (100, 100)", 20, 265);
    canvas.fillText("  ESC: Exit", 20, 280);
}

VIVID_CHAIN(setup, update)
