// VJ Mixer - Vivid Showcase
// Multi-layer video mixing with geometry flashes, typography, and effects
//
// Controls:
//   1-4: Toggle video layers on/off
//   Q/W/E/R: Blend modes (Add/Screen/Multiply/Difference)
//   SPACE: Trigger flash effect
//   T: Flash random text
//   G: Flash 2D geometry
//   F: Toggle feedback trails
//   UP/DOWN: Crossfade between video pairs
//   TAB: Parameter controls

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>
#include <cmath>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

// =============================================================================
// State
// =============================================================================

static bool g_layerEnabled[4] = {true, true, false, false};
static float g_layerOpacity[4] = {1.0f, 0.5f, 0.5f, 0.5f};
static BlendMode g_blendMode = BlendMode::Add;
static float g_crossfade = 0.5f;  // 0 = layer 1+2, 1 = layer 3+4

// Geometry flash state
static bool g_showGeometry = false;
static int g_geometryType = 0;
static float g_geometryAlpha = 1.0f;
static float g_geometryScale = 1.0f;
static glm::vec4 g_geometryColor = {1.0f, 1.0f, 1.0f, 1.0f};

// Text flash state
static bool g_showText = false;
static const char* g_textWord = "VIVID";
static float g_textAlpha = 1.0f;
static float g_textScale = 1.0f;
static glm::vec4 g_textColor = {1.0f, 1.0f, 1.0f, 1.0f};

// Effect state
static float g_flashIntensity = 0.0f;
static bool g_feedbackEnabled = false;

// Word bank for text flashes
static const char* g_words[] = {
    "DROP", "BASS", "VIVID", "BEAT", "FLOW",
    "SYNC", "PULSE", "WAVE", "HYPE", "FIRE"
};
static const int NUM_WORDS = 10;

// Random color palette
static const glm::vec4 g_colors[] = {
    {1.0f, 0.2f, 0.4f, 1.0f},   // Hot pink
    {0.2f, 0.8f, 1.0f, 1.0f},   // Cyan
    {1.0f, 0.8f, 0.0f, 1.0f},   // Gold
    {0.6f, 0.2f, 1.0f, 1.0f},   // Purple
    {0.2f, 1.0f, 0.4f, 1.0f},   // Neon green
    {1.0f, 0.4f, 0.0f, 1.0f},   // Orange
};
static const int NUM_COLORS = 6;

void printStatus() {
    std::cout << "\r[";
    for (int i = 0; i < 4; i++) {
        std::cout << (g_layerEnabled[i] ? (i + 1) : '-');
    }
    std::cout << "] Blend: ";
    switch (g_blendMode) {
        case BlendMode::Add: std::cout << "ADD"; break;
        case BlendMode::Screen: std::cout << "SCR"; break;
        case BlendMode::Multiply: std::cout << "MUL"; break;
        case BlendMode::Difference: std::cout << "DIF"; break;
        default: std::cout << "???"; break;
    }
    std::cout << " Fade: " << static_cast<int>(g_crossfade * 100) << "%   " << std::flush;
}

// =============================================================================
// Setup
// =============================================================================

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Video Layers - Users should replace these paths with their own videos
    // =========================================================================

    auto& v1 = chain.add<VideoPlayer>("video1");
    v1.file = "assets/videos/loop1.mov";  // Replace with your video
    v1.loop(true);

    auto& v2 = chain.add<VideoPlayer>("video2");
    v2.file = "assets/videos/loop2.mov";  // Replace with your video
    v2.loop(true);

    auto& v3 = chain.add<VideoPlayer>("video3");
    v3.file = "assets/videos/loop3.mov";  // Replace with your video
    v3.loop(true);

    auto& v4 = chain.add<VideoPlayer>("video4");
    v4.file = "assets/videos/loop4.mov";  // Replace with your video
    v4.loop(true);

    // =========================================================================
    // Fallback: Generated Content (when videos aren't available)
    // =========================================================================

    // Noise patterns as fallback/overlay content
    auto& noise1 = chain.add<Noise>("noise1");
    noise1.scale = 3.0f;
    noise1.speed = 0.3f;
    noise1.type(NoiseType::Simplex);

    auto& noise2 = chain.add<Noise>("noise2");
    noise2.scale = 8.0f;
    noise2.speed = 0.5f;
    noise2.type(NoiseType::Worley);

    // Color the noise
    auto& colored1 = chain.add<HSV>("colored1");
    colored1.input("noise1");
    colored1.hueShift = 0.0f;
    colored1.saturation = 1.2f;

    auto& colored2 = chain.add<HSV>("colored2");
    colored2.input("noise2");
    colored2.hueShift = 0.5f;
    colored2.saturation = 1.2f;

    // =========================================================================
    // Video Mixer - 4-layer composite
    // =========================================================================

    // Layer 1+2 mix
    auto& mix12 = chain.add<Composite>("mix12");
    mix12.input(0, "colored1");  // Use noise as fallback
    mix12.input(1, "colored2");
    mix12.mode(BlendMode::Add);

    // Layer 3+4 mix (uses same sources for demo)
    auto& mix34 = chain.add<Composite>("mix34");
    mix34.input(0, "noise1");
    mix34.input(1, "noise2");
    mix34.mode(BlendMode::Screen);

    // Crossfade between pairs
    auto& mixer = chain.add<Composite>("mixer");
    mixer.inputA("mix12");
    mixer.inputB("mix34");
    mixer.mode(BlendMode::Over);
    mixer.opacity(0.0f);  // Full layer A by default

    // =========================================================================
    // Geometry Canvas
    // =========================================================================

    auto& shapes = chain.add<Canvas>("shapes");
    shapes.size(1920, 1080);

    // Composite shapes over video
    auto& withShapes = chain.add<Composite>("withShapes");
    withShapes.inputA("mixer");
    withShapes.inputB("shapes");
    withShapes.mode(BlendMode::Add);

    // =========================================================================
    // Typography Canvas
    // =========================================================================

    auto& text = chain.add<Canvas>("text");
    text.size(1920, 1080);

    // Try to load a bold font (users can provide their own)
    if (!text.loadFont(ctx, "assets/fonts/space age.ttf", 180.0f)) {
        std::cout << "Note: Could not load font. Text features disabled." << std::endl;
        std::cout << "      Place a TTF font at assets/fonts/space age.ttf" << std::endl;
    }

    // Composite text over shapes
    auto& withText = chain.add<Composite>("withText");
    withText.inputA("withShapes");
    withText.inputB("text");
    withText.mode(BlendMode::Add);

    // =========================================================================
    // Post Effects
    // =========================================================================

    // Feedback for trails
    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input("withText");
    feedback.decay = 0.85f;
    feedback.mix = 0.0f;  // Off by default

    // Bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("feedback");
    bloom.threshold = 0.4f;
    bloom.intensity = 0.6f;
    bloom.radius = 15.0f;

    // Chromatic aberration (triggered on hits)
    auto& chroma = chain.add<ChromaticAberration>("chroma");
    chroma.input("bloom");
    chroma.amount = 0.0f;

    // Color cycling
    auto& hsv = chain.add<HSV>("finalColor");
    hsv.input("chroma");
    hsv.hueShift = 0.0f;

    chain.output("finalColor");

    // =========================================================================
    // Console Output
    // =========================================================================

    std::cout << "\n========================================" << std::endl;
    std::cout << "VJ Mixer - Vivid Showcase" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  1-4: Toggle video layers" << std::endl;
    std::cout << "  Q/W/E/R: Blend modes" << std::endl;
    std::cout << "  SPACE: Flash effect" << std::endl;
    std::cout << "  T: Flash text" << std::endl;
    std::cout << "  G: Flash geometry" << std::endl;
    std::cout << "  F: Toggle feedback" << std::endl;
    std::cout << "  UP/DOWN: Crossfade" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "\nPlace your videos at:" << std::endl;
    std::cout << "  assets/videos/loop1.mov" << std::endl;
    std::cout << "  assets/videos/loop2.mov" << std::endl;
    std::cout << "  assets/videos/loop3.mov" << std::endl;
    std::cout << "  assets/videos/loop4.mov" << std::endl;
    std::cout << "========================================\n" << std::endl;

    printStatus();
}

// =============================================================================
// Update
// =============================================================================

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());
    float dt = static_cast<float>(ctx.dt());

    // Get operators
    auto& shapes = chain.get<Canvas>("shapes");
    auto& text = chain.get<Canvas>("text");
    auto& mixer = chain.get<Composite>("mixer");
    auto& mix12 = chain.get<Composite>("mix12");
    auto& feedback = chain.get<Feedback>("feedback");
    auto& chroma = chain.get<ChromaticAberration>("chroma");
    auto& hsv = chain.get<HSV>("finalColor");
    auto& colored1 = chain.get<HSV>("colored1");
    auto& colored2 = chain.get<HSV>("colored2");

    // Clear canvases each frame
    shapes.clear(0, 0, 0, 0);  // Transparent
    text.clear(0, 0, 0, 0);

    // =========================================================================
    // Input Handling
    // =========================================================================

    // Layer toggles (1-4)
    if (ctx.key(GLFW_KEY_1).pressed) { g_layerEnabled[0] = !g_layerEnabled[0]; printStatus(); }
    if (ctx.key(GLFW_KEY_2).pressed) { g_layerEnabled[1] = !g_layerEnabled[1]; printStatus(); }
    if (ctx.key(GLFW_KEY_3).pressed) { g_layerEnabled[2] = !g_layerEnabled[2]; printStatus(); }
    if (ctx.key(GLFW_KEY_4).pressed) { g_layerEnabled[3] = !g_layerEnabled[3]; printStatus(); }

    // Blend modes (Q/W/E/R)
    if (ctx.key(GLFW_KEY_Q).pressed) { g_blendMode = BlendMode::Add; mix12.mode(g_blendMode); printStatus(); }
    if (ctx.key(GLFW_KEY_W).pressed) { g_blendMode = BlendMode::Screen; mix12.mode(g_blendMode); printStatus(); }
    if (ctx.key(GLFW_KEY_E).pressed) { g_blendMode = BlendMode::Multiply; mix12.mode(g_blendMode); printStatus(); }
    if (ctx.key(GLFW_KEY_R).pressed) { g_blendMode = BlendMode::Difference; mix12.mode(g_blendMode); printStatus(); }

    // Crossfade (UP/DOWN)
    if (ctx.key(GLFW_KEY_UP).held) {
        g_crossfade = std::min(1.0f, g_crossfade + dt * 0.5f);
        mixer.opacity(g_crossfade);
        printStatus();
    }
    if (ctx.key(GLFW_KEY_DOWN).held) {
        g_crossfade = std::max(0.0f, g_crossfade - dt * 0.5f);
        mixer.opacity(g_crossfade);
        printStatus();
    }

    // Flash effect (SPACE)
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        g_flashIntensity = 1.0f;
    }

    // Text flash (T)
    if (ctx.key(GLFW_KEY_T).pressed) {
        g_showText = true;
        g_textWord = g_words[rand() % NUM_WORDS];
        g_textAlpha = 1.0f;
        g_textScale = 1.0f;
        g_textColor = g_colors[rand() % NUM_COLORS];
    }

    // Geometry flash (G)
    if (ctx.key(GLFW_KEY_G).pressed) {
        g_showGeometry = true;
        g_geometryType = rand() % 4;  // Circle, Triangle, Rectangle, or Ring
        g_geometryAlpha = 1.0f;
        g_geometryScale = 1.0f;
        g_geometryColor = g_colors[rand() % NUM_COLORS];
    }

    // Toggle feedback (F)
    if (ctx.key(GLFW_KEY_F).pressed) {
        g_feedbackEnabled = !g_feedbackEnabled;
        std::cout << "\nFeedback: " << (g_feedbackEnabled ? "ON" : "OFF") << std::endl;
        printStatus();
    }

    // =========================================================================
    // Animate Colors
    // =========================================================================

    // Slowly shift hues over time
    colored1.hueShift = std::fmod(time * 0.05f, 1.0f);
    colored2.hueShift = std::fmod(time * 0.03f + 0.5f, 1.0f);

    // Final color cycling
    hsv.hueShift = std::fmod(time * 0.02f, 1.0f);

    // =========================================================================
    // Draw Geometry
    // =========================================================================

    if (g_showGeometry) {
        float cx = 960.0f;
        float cy = 540.0f;
        float size = 150.0f * g_geometryScale;

        shapes.fillStyle(g_geometryColor.r, g_geometryColor.g, g_geometryColor.b, g_geometryAlpha);

        switch (g_geometryType) {
            case 0:  // Circle
                shapes.fillCircle(cx, cy, size, 48);
                break;

            case 1:  // Triangle
                shapes.beginPath();
                shapes.moveTo(cx, cy - size);
                shapes.lineTo(cx - size * 0.866f, cy + size * 0.5f);
                shapes.lineTo(cx + size * 0.866f, cy + size * 0.5f);
                shapes.closePath();
                shapes.fill();
                break;

            case 2:  // Rectangle
                shapes.fillRect(cx - size, cy - size * 0.6f, size * 2, size * 1.2f);
                break;

            case 3:  // Ring (circle outline)
                shapes.strokeStyle(g_geometryColor.r, g_geometryColor.g, g_geometryColor.b, g_geometryAlpha);
                shapes.lineWidth(size * 0.15f);
                shapes.strokeCircle(cx, cy, size, 48);
                break;
        }

        // Animate: fade out and scale up
        g_geometryAlpha *= 0.92f;
        g_geometryScale *= 1.03f;

        if (g_geometryAlpha < 0.01f) {
            g_showGeometry = false;
        }
    }

    // =========================================================================
    // Draw Text
    // =========================================================================

    if (g_showText) {
        float cx = 960.0f;
        float cy = 540.0f;

        text.save();
        text.translate(cx, cy);
        text.scale(g_textScale, g_textScale);

        text.fillStyle(g_textColor.r, g_textColor.g, g_textColor.b, g_textAlpha);
        text.textAlign(TextAlign::Center);
        text.textBaseline(TextBaseline::Middle);
        text.fillText(g_textWord, 0, 0);

        text.restore();

        // Animate: fade out and scale up
        g_textAlpha *= 0.93f;
        g_textScale *= 1.02f;

        if (g_textAlpha < 0.01f) {
            g_showText = false;
        }
    }

    // =========================================================================
    // Update Effects
    // =========================================================================

    // Flash intensity decay
    g_flashIntensity *= 0.88f;

    // Chromatic aberration follows flash
    chroma.amount = g_flashIntensity * 0.025f;

    // Feedback mix
    if (g_feedbackEnabled) {
        feedback.mix = 0.4f + g_flashIntensity * 0.3f;
        feedback.decay = 0.88f;
    } else {
        feedback.mix = g_flashIntensity * 0.5f;
    }
}

VIVID_CHAIN(setup, update)
