// Wipeout 2029 - Procedural Anti-Gravity Craft Showcase
// Modular craft geometry with customizable part classes
//
// Demonstrates: MeshBuilder, SceneComposer, Render3D, Downsample, Dither, CRTEffect
//
// Controls:
//   Mouse drag: Orbit camera
//   Scroll: Zoom in/out
//   1-5: Select team (FEISAR, AG-SYS, AURICOM, QIREX, PIRANHA)
//   V: Toggle VertexLit/PBR shading
//   TAB: Open parameter controls

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include "craft.h"
#include <cmath>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// =============================================================================
// Team Color Palettes
// =============================================================================

struct TeamPalette {
    const char* name;
    const char* number;
    Color primary;
    Color secondary;
    Color accent;
};

static const TeamPalette g_teams[] = {
    {"FEISAR",  "01", Color::fromHex("#2B5CB0"), Color::White,              Color::Gold},                 // Blue/White/Gold
    {"AG-SYS",  "07", Color::Gold,               Color::fromHex("#0066CC"), Color::White},                // Yellow/Blue/White
    {"AURICOM", "12", Color::fromHex("#CC0000"), Color::White,              Color::DimGray},              // Red/White/Gray
    {"QIREX",   "23", Color::fromHex("#6B0099"), Color::DarkTurquoise,      Color::fromHex("#1A1A1A")},   // Purple/Cyan/Black
    {"PIRANHA", "42", Color::DimGray,            Color::OrangeRed,          Color::Silver},               // Charcoal/Orange/Silver
};

// Font loaded flag
static bool g_fontLoaded = false;

static int g_currentTeam = 0;

// Debug mode toggle (V key) - shows untextured wireframe-style view
static bool g_debugMode = false;

// Camera orbit state
static float g_cameraYaw = 0.0f;
static float g_cameraPitch = 0.3f;
static float g_cameraDistance = 5.0f;
static glm::vec2 g_lastMouse = {0, 0};

// Craft instance - can be customized before building
static Craft g_craft;

// =============================================================================
// UI Drawing Helper
// =============================================================================

void drawUI(Canvas& canvas, const TeamPalette& team, bool debugMode) {
    // Clear with transparent background
    canvas.clear(0, 0, 0, 0);

    // Team info box (bottom-left)
    float boxX = 40;
    float boxY = 640;
    float boxW = 220;
    float boxH = 60;

    // Semi-transparent background
    canvas.rectFilled(boxX, boxY, boxW, boxH, {0.0f, 0.0f, 0.0f, 0.7f});

    // Team color accent bar
    canvas.rectFilled(boxX, boxY, 6, boxH, team.primary);

    // Team name and number
    canvas.text(team.name, boxX + 20, boxY + 28, team.secondary);
    canvas.text(team.number, boxX + boxW - 50, boxY + 28, team.accent);

    // Shading mode indicator (bottom-right)
    float modeX = 1020;
    float modeY = 640;
    float modeW = 220;
    float modeH = 60;

    canvas.rectFilled(modeX, modeY, modeW, modeH, {0.0f, 0.0f, 0.0f, 0.7f});

    // Color indicator for mode
    glm::vec4 modeColor = debugMode ? glm::vec4(0.8f, 0.4f, 0.2f, 1.0f) : glm::vec4(0.2f, 0.8f, 0.6f, 1.0f);
    canvas.rectFilled(modeX + modeW - 6, modeY, 6, modeH, modeColor);

    // Render mode text
    const char* modeText = debugMode ? "DEBUG MODE" : "TEXTURED";
    canvas.text(modeText, modeX + 15, modeY + 28, {1.0f, 1.0f, 1.0f, 1.0f});
}

// =============================================================================
// Livery Drawing Helper
// =============================================================================

void drawLivery(Canvas& canvas, const TeamPalette& team, bool fontLoaded) {
    const float w = 512.0f;
    const float h = 512.0f;

    // =========================================================================
    // Base Layer - Primary team color
    // =========================================================================
    canvas.clear(team.primary.r, team.primary.g, team.primary.b, 1.0f);

    // DEBUG: Test basic rectangle drawing - use team secondary color for visibility
    canvas.rectFilled(100, 100, 200, 200, team.secondary);  // Secondary color square
    canvas.rectFilled(150, 150, 100, 100, team.accent);     // Accent color square inside

    // =========================================================================
    // Racing Stripes - Secondary color bands
    // =========================================================================

    // Main horizontal racing stripe
    canvas.rectFilled(0, h * 0.35f, w, h * 0.12f, team.secondary);

    // Thin accent stripe below main stripe
    canvas.rectFilled(0, h * 0.48f, w, h * 0.02f, team.accent);

    // =========================================================================
    // Sponsor Graphics - Geometric shapes and decals
    // =========================================================================

    // Chevron pattern (left side) - sponsor-style angular graphics
    glm::vec4 chevronColor = {team.secondary.r, team.secondary.g, team.secondary.b, 0.7f};
    for (int i = 0; i < 3; ++i) {
        float yBase = h * 0.55f + i * 25.0f;
        canvas.triangleFilled({0, yBase}, {60.0f, yBase + 12.0f}, {0, yBase + 24.0f}, chevronColor);
    }

    // Chevron pattern (right side) - mirrored
    for (int i = 0; i < 3; ++i) {
        float yBase = h * 0.55f + i * 25.0f;
        canvas.triangleFilled({w, yBase}, {w - 60.0f, yBase + 12.0f}, {w, yBase + 24.0f}, chevronColor);
    }

    // AG logo block (top area) - anti-gravity racing logo placeholder
    canvas.rectFilled(w * 0.1f, h * 0.08f, 80.0f, 35.0f, team.secondary);
    canvas.rectFilled(w * 0.1f + 4.0f, h * 0.08f + 4.0f, 72.0f, 27.0f, team.primary);

    // Speed stripe decals (diagonal accent lines on sides)
    for (int i = 0; i < 4; ++i) {
        float x = w * 0.75f + i * 15.0f;
        canvas.line(x, h * 0.15f, x + 30.0f, h * 0.28f, 4.0f, team.accent);
    }

    // Hazard stripes at rear (bottom edge)
    Color hazardDark = Color::fromHex("#1A1A1A");
    Color hazardBright = team.accent;
    float stripeW = 25.0f;
    for (int i = 0; i < 22; ++i) {
        glm::vec4 color = (i % 2 == 0) ? hazardDark : hazardBright;
        canvas.triangleFilled({i * stripeW, h}, {(i + 1) * stripeW, h}, {i * stripeW + stripeW * 0.5f, h - 20.0f}, color);
    }

    // =========================================================================
    // Panel Lines - Surface detail for mechanical look
    // =========================================================================

    Color panelLine = Color::Black.withAlpha(0.4f);
    Color panelLineLight = Color::White.withAlpha(0.15f);

    // Horizontal panel seams
    canvas.rectFilled(0, h * 0.22f, w, 2.0f, panelLine);
    canvas.rectFilled(0, h * 0.22f + 3.0f, w, 1.0f, panelLineLight);  // Highlight

    canvas.rectFilled(0, h * 0.52f, w, 2.0f, panelLine);
    canvas.rectFilled(0, h * 0.52f + 3.0f, w, 1.0f, panelLineLight);

    canvas.rectFilled(0, h * 0.72f, w, 2.0f, panelLine);
    canvas.rectFilled(0, h * 0.72f + 3.0f, w, 1.0f, panelLineLight);

    // Vertical panel seams
    canvas.rectFilled(w * 0.3f, 0, 2.0f, h * 0.35f, panelLine);
    canvas.rectFilled(w * 0.7f, 0, 2.0f, h * 0.35f, panelLine);
    canvas.rectFilled(w * 0.3f, h * 0.5f, 2.0f, h * 0.22f, panelLine);
    canvas.rectFilled(w * 0.7f, h * 0.5f, 2.0f, h * 0.22f, panelLine);

    // Rivet lines (small dots along panel seams)
    Color rivetColor = Color::DimGray.withAlpha(0.6f);
    for (int i = 0; i < 16; ++i) {
        float x = 20.0f + i * 30.0f;
        canvas.circleFilled(x, h * 0.22f - 6.0f, 2.5f, rivetColor, 6);
        canvas.circleFilled(x, h * 0.52f - 6.0f, 2.5f, rivetColor, 6);
    }

    // =========================================================================
    // Weathering & Grime - Edge darkening and wear marks
    // =========================================================================

    // Edge darkening (vignette-style grime on borders)
    Color grime = Color::Black.withAlpha(0.25f);
    canvas.rectFilled(0, 0, w, 15.0f, grime);              // Top edge
    canvas.rectFilled(0, h - 25.0f, w, 25.0f, grime);      // Bottom edge (heavier)
    canvas.rectFilled(0, 0, 12.0f, h, grime);              // Left edge
    canvas.rectFilled(w - 12.0f, 0, 12.0f, h, grime);      // Right edge

    // Exhaust staining (dark streaks near rear)
    Color exhaustStain = Color::Black.withAlpha(0.15f);
    for (int i = 0; i < 5; ++i) {
        float x = w * 0.2f + i * 70.0f;
        canvas.rectFilled(x, h * 0.85f, 8.0f, h * 0.15f, exhaustStain);
    }

    // Random scratches (thin diagonal lines)
    Color scratch = Color::Black.withAlpha(0.2f);
    canvas.line(100, 180, 130, 200, 1.0f, scratch);
    canvas.line(320, 90, 360, 110, 1.0f, scratch);
    canvas.line(420, 280, 445, 310, 1.0f, scratch);
    canvas.line(80, 400, 110, 430, 1.0f, scratch);
    canvas.line(380, 380, 400, 420, 1.0f, scratch);

    // Scuff marks (small semi-transparent rectangles)
    Color scuff = Color::Black.withAlpha(0.12f);
    canvas.rectFilled(150, 300, 25, 8, scuff);
    canvas.rectFilled(350, 150, 20, 10, scuff);
    canvas.rectFilled(70, 450, 30, 12, scuff);
    canvas.rectFilled(430, 350, 22, 9, scuff);

    // =========================================================================
    // Team Number - Bold racing number in circle
    // =========================================================================

    if (fontLoaded) {
        // Number background circle with border
        canvas.circleFilled(w * 0.5f, h * 0.78f, 48.0f, team.secondary, 32);
        canvas.circle(w * 0.5f, h * 0.78f, 48.0f, 4.0f, team.accent, 32);
        canvas.circle(w * 0.5f, h * 0.78f, 42.0f, 2.0f, team.primary, 32);

        // Team number text
        canvas.textCentered(team.number, w * 0.5f, h * 0.78f + 8.0f, team.primary);
    }

    // =========================================================================
    // Accent Blocks - Side pod color blocks
    // =========================================================================

    canvas.rectFilled(0, h * 0.6f, w * 0.08f, h * 0.12f, team.accent);
    canvas.rectFilled(w * 0.92f, h * 0.6f, w * 0.08f, h * 0.12f, team.accent);
}

// =============================================================================
// Setup
// =============================================================================

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Livery Texture - Canvas-based procedural texture
    // =========================================================================

    auto& livery = chain.add<Canvas>("livery").size(512, 512);

    // Load font for livery text (team numbers)
    if (livery.loadFont(ctx, "assets/fonts/Pixeled.ttf", 48.0f)) {
        g_fontLoaded = true;
    } else {
        std::cout << "Warning: Could not load livery font" << std::endl;
    }

    // =========================================================================
    // Craft Material - TexturedMaterial with Canvas input
    // =========================================================================

    auto& material = chain.add<TexturedMaterial>("material")
        .baseColorInput(&livery)
        .roughnessFactor(0.7f)
        .metallicFactor(0.1f);

    // =========================================================================
    // Craft Geometry - Modular Class-based Design
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Customize craft parts if desired (optional)
    // g_craft.fuselage.length = 1.6f;
    // g_craft.leftPod.bodyLength = 1.2f;
    // g_craft.rightPod.bodyLength = 1.2f;

    // Build the aerodynamic craft mesh and add with textured material
    auto craftMesh = g_craft.build();
    auto& craftStatic = chain.add<StaticMesh>("craftMesh");
    craftStatic.mesh(craftMesh);
    scene.add(&craftStatic, &material);

    // =========================================================================
    // Engine Glow - Emissive Material
    // =========================================================================

    auto& glowMaterial = chain.add<TexturedMaterial>("glowMaterial")
        .baseColorFactor(1.0f, 0.6f, 0.2f, 1.0f)   // Orange base
        .emissiveFactor(1.0f, 0.5f, 0.1f)          // Orange glow
        .emissiveStrength(3.0f)                     // Bright emission
        .metallicFactor(0.0f)
        .roughnessFactor(1.0f);

    // Build engine glow mesh and add with emissive material
    auto glowMesh = g_craft.buildEngineGlow();
    auto& glowStatic = chain.add<StaticMesh>("glowMesh");
    glowStatic.mesh(glowMesh);
    scene.add(&glowStatic, &glowMaterial);

    // =========================================================================
    // Camera and Lighting
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera")
        .position(0, 1.5f, -5.0f)
        .target(0, 0, 0)
        .fov(45.0f);

    auto& keyLight = chain.add<DirectionalLight>("keyLight")
        .direction(1.0f, 2.0f, 1.0f)
        .color(1.0f, 0.95f, 0.9f)  // Warm white
        .intensity(1.5f);

    auto& fillLight = chain.add<DirectionalLight>("fillLight")
        .direction(-1.0f, 0.5f, -1.0f)
        .color(0.4f, 0.6f, 0.9f)  // Cool blue fill
        .intensity(0.4f);

    // =========================================================================
    // 3D Render
    // =========================================================================

    auto& render = chain.add<Render3D>("render")
        .input(&scene)
        .cameraInput(&camera)
        .lightInput(&keyLight)
        .lightInput(&fillLight)
        .material(&material)
        .shadingMode(ShadingMode::PBR)
        .clearColor(0.02f, 0.02f, 0.05f)
        .resolution(1280, 720);

    // =========================================================================
    // Retro Post-Processing Pipeline
    // =========================================================================

    // Downsample to low resolution (PS1-style)
    auto& downsample = chain.add<Downsample>("downsample")
        .input(&render)
        .resolution(480, 270)
        .filter(FilterMode::Nearest);

    // Dither for color banding
    auto& dither = chain.add<Dither>("dither")
        .input(&downsample)
        .pattern(DitherPattern::Bayer4x4)
        .levels(32)
        .strength(0.7f);

    // CRT effect (scanlines, vignette, slight curvature)
    auto& crt = chain.add<CRTEffect>("crt")
        .input(&dither)
        .curvature(0.08f)
        .scanlines(0.15f)
        .vignette(0.3f)
        .bloom(0.1f)
        .chromatic(0.015f);

    // =========================================================================
    // UI Overlay - Team name and shading mode
    // =========================================================================

    auto& ui = chain.add<Canvas>("ui").size(1280, 720);

    // Load font for UI text (smaller size)
    if (!ui.loadFont(ctx, "assets/fonts/space age.ttf", 24.0f)) {
        std::cout << "Warning: Could not load UI font" << std::endl;
    }

    // Composite UI over CRT output
    auto& composite = chain.add<Composite>("composite")
        .input(0, &crt)
        .input(1, &ui)
        .mode(BlendMode::Over);

    chain.output("composite");

    // Print initial state
    std::cout << "Team: " << g_teams[g_currentTeam].name << std::endl;
    std::cout << "Mode: Textured (press V for debug mode)" << std::endl;
}

// =============================================================================
// Update
// =============================================================================

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // =========================================================================
    // Draw Livery Texture and UI
    // =========================================================================

    auto& livery = chain.get<Canvas>("livery");
    drawLivery(livery, g_teams[g_currentTeam], g_fontLoaded);

    auto& ui = chain.get<Canvas>("ui");
    drawUI(ui, g_teams[g_currentTeam], g_debugMode);

    // =========================================================================
    // Input: Toggle shading mode
    // =========================================================================

    if (ctx.key(GLFW_KEY_V).pressed) {
        g_debugMode = !g_debugMode;
        auto& render = chain.get<Render3D>("render");
        render.shadingMode(g_debugMode ? ShadingMode::VertexLit : ShadingMode::PBR);
        std::cout << "Mode: " << (g_debugMode ? "Debug (untextured)" : "Textured") << std::endl;
    }

    // =========================================================================
    // Input: Team selection (1-5 keys)
    // =========================================================================

    int newTeam = -1;
    if (ctx.key(GLFW_KEY_1).pressed) newTeam = 0;
    if (ctx.key(GLFW_KEY_2).pressed) newTeam = 1;
    if (ctx.key(GLFW_KEY_3).pressed) newTeam = 2;
    if (ctx.key(GLFW_KEY_4).pressed) newTeam = 3;
    if (ctx.key(GLFW_KEY_5).pressed) newTeam = 4;

    if (newTeam >= 0 && newTeam != g_currentTeam) {
        g_currentTeam = newTeam;
        // Team colors update automatically via livery texture redraw
        std::cout << "Team: " << g_teams[g_currentTeam].name << std::endl;
    }

    // =========================================================================
    // Camera Orbit
    // =========================================================================

    glm::vec2 currentMouse = ctx.mouse();
    glm::vec2 mouseDelta = currentMouse - g_lastMouse;
    g_lastMouse = currentMouse;

    if (ctx.mouseButton(0).held) {
        g_cameraYaw -= mouseDelta.x * 0.005f;
        g_cameraPitch -= mouseDelta.y * 0.005f;
        g_cameraPitch = glm::clamp(g_cameraPitch, -1.2f, 1.2f);
    }

    // Scroll to zoom
    g_cameraDistance -= ctx.scroll().y * 0.3f;
    g_cameraDistance = glm::clamp(g_cameraDistance, 2.0f, 15.0f);

    // Slow auto-rotate when not dragging
    if (!ctx.mouseButton(0).held) {
        g_cameraYaw += static_cast<float>(ctx.dt()) * 0.15f;
    }

    // Calculate camera position from spherical coordinates
    float camX = g_cameraDistance * cos(g_cameraPitch) * sin(g_cameraYaw);
    float camY = g_cameraDistance * sin(g_cameraPitch) + 0.5f;
    float camZ = g_cameraDistance * cos(g_cameraPitch) * cos(g_cameraYaw);

    auto& camera = chain.get<CameraOperator>("camera");
    camera.position(camX, camY, camZ);

    // =========================================================================
    // Hover Animation - apply to entire scene
    // =========================================================================

    float hover = sin(time * 1.5f) * 0.04f;
    float roll = sin(time * 0.7f) * 0.02f;

    glm::mat4 hoverMat = glm::translate(glm::mat4(1.0f), glm::vec3(0, hover, 0));
    hoverMat = glm::rotate(hoverMat, roll, glm::vec3(1, 0, 0));

    auto& scene = chain.get<SceneComposer>("scene");
    scene.rootTransform(hoverMat);
}

VIVID_CHAIN(setup, update)
