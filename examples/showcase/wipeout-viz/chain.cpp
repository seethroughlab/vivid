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
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include "craft.h"
#include <cmath>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;
using namespace vivid::audio;

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
static float g_cameraYaw = 0.5f;         // 3/4 front view
static float g_cameraPitch = 0.4f;       // Slight tilt
static float g_cameraDistance = 5.0f;    // Good viewing distance
static glm::vec2 g_lastMouse = {0, 0};

// Craft instance - can be customized before building
static Craft g_craft;

// =============================================================================
// Grid Floor Drawing Helper
// =============================================================================

void drawGridFloor(Canvas& canvas, const TeamPalette& team, float time) {
    const float w = 512.0f;
    const float h = 512.0f;

    // Dark base with slight gradient feel
    canvas.clear(0.015f, 0.015f, 0.025f, 1.0f);

    // Grid colors - subtle neon glow matching team accent
    Color gridColor = team.accent.withAlpha(0.25f);
    Color gridBright = team.accent.withAlpha(0.5f);

    // Draw grid lines
    float gridSize = 32.0f;  // Spacing between lines
    float lineWidth = 1.5f;

    // Vertical lines
    for (float x = 0; x <= w; x += gridSize) {
        float alpha = (fmod(x, gridSize * 4) < 0.1f) ? 0.5f : 0.25f;  // Every 4th line brighter
        canvas.rectFilled(x - lineWidth * 0.5f, 0, lineWidth, h, team.accent.withAlpha(alpha));
    }

    // Horizontal lines
    for (float y = 0; y <= h; y += gridSize) {
        float alpha = (fmod(y, gridSize * 4) < 0.1f) ? 0.5f : 0.25f;
        canvas.rectFilled(0, y - lineWidth * 0.5f, w, lineWidth, team.accent.withAlpha(alpha));
    }

    // Center highlight cross (runway markers)
    float centerX = w * 0.5f;
    float centerY = h * 0.5f;
    canvas.rectFilled(centerX - 2.0f, 0, 4.0f, h, gridBright);
    canvas.rectFilled(0, centerY - 2.0f, w, 4.0f, gridBright);

    // Corner accent marks
    float markSize = 24.0f;
    Color cornerColor = team.secondary.withAlpha(0.6f);
    // Top-left
    canvas.rectFilled(0, 0, markSize, 3.0f, cornerColor);
    canvas.rectFilled(0, 0, 3.0f, markSize, cornerColor);
    // Top-right
    canvas.rectFilled(w - markSize, 0, markSize, 3.0f, cornerColor);
    canvas.rectFilled(w - 3.0f, 0, 3.0f, markSize, cornerColor);
    // Bottom-left
    canvas.rectFilled(0, h - 3.0f, markSize, 3.0f, cornerColor);
    canvas.rectFilled(0, h - markSize, 3.0f, markSize, cornerColor);
    // Bottom-right
    canvas.rectFilled(w - markSize, h - 3.0f, markSize, 3.0f, cornerColor);
    canvas.rectFilled(w - 3.0f, h - markSize, 3.0f, markSize, cornerColor);
}

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
// Normal Map Drawing Helper
// =============================================================================

void drawNormalMap(Canvas& canvas) {
    const float w = 1024.0f;
    const float h = 1024.0f;

    // Neutral normal (flat surface pointing up in tangent space)
    // RGB = (0.5, 0.5, 1.0) means normal pointing straight out
    glm::vec4 neutral(0.5f, 0.5f, 1.0f, 1.0f);
    canvas.clear(neutral.r, neutral.g, neutral.b, neutral.a);

    // Panel lines - slight inward normal to create recessed look
    // Darken R (X) slightly on left edge, lighten on right edge of lines
    glm::vec4 lineLeft(0.45f, 0.5f, 0.95f, 1.0f);   // Normal tilted left
    glm::vec4 lineRight(0.55f, 0.5f, 0.95f, 1.0f);  // Normal tilted right
    glm::vec4 lineUp(0.5f, 0.45f, 0.95f, 1.0f);     // Normal tilted up
    glm::vec4 lineDown(0.5f, 0.55f, 0.95f, 1.0f);   // Normal tilted down

    // Horizontal panel lines with beveled edges
    for (float y : {0.25f, 0.45f, 0.65f, 0.85f}) {
        canvas.rectFilled(0, h * y - 2, w, 2, lineUp);
        canvas.rectFilled(0, h * y, w, 2, lineDown);
    }

    // Vertical panel lines
    for (float x : {0.2f, 0.4f, 0.6f, 0.8f}) {
        canvas.rectFilled(w * x - 2, 0, 2, h, lineLeft);
        canvas.rectFilled(w * x, 0, 2, h, lineRight);
    }

    // Racing stripe edges - raised feel
    glm::vec4 stripeTop(0.5f, 0.4f, 0.98f, 1.0f);    // Top edge of raised stripe
    glm::vec4 stripeBot(0.5f, 0.6f, 0.98f, 1.0f);    // Bottom edge
    canvas.rectFilled(0, h * 0.33f, w, 3, stripeTop);
    canvas.rectFilled(0, h * 0.47f - 3, w, 3, stripeBot);

    // Surface texture variation - subtle noise-like bumps
    // Add some random-ish bumps for surface detail
    for (int i = 0; i < 50; i++) {
        float bx = (float)((i * 73) % 100) / 100.0f * w;
        float by = (float)((i * 137) % 100) / 100.0f * h;
        float angle = (float)((i * 47) % 360);
        float nx = 0.5f + 0.08f * cos(glm::radians(angle));
        float ny = 0.5f + 0.08f * sin(glm::radians(angle));
        canvas.circleFilled(bx, by, 8 + (i % 5) * 2, {nx, ny, 0.97f, 1.0f});
    }
}

// =============================================================================
// Roughness Map Drawing Helper
// =============================================================================

void drawRoughnessMap(Canvas& canvas, const TeamPalette& team) {
    const float w = 1024.0f;
    const float h = 1024.0f;

    // Base roughness - moderately rough painted surface
    // Roughness in R channel: 0 = smooth/shiny, 1 = rough/matte
    float baseRoughness = 0.65f;
    canvas.clear(baseRoughness, baseRoughness, baseRoughness, 1.0f);

    // Racing stripe area - slightly smoother (fresher paint)
    float stripeRoughness = 0.55f;
    canvas.rectFilled(0, h * 0.33f, w, h * 0.14f, {stripeRoughness, stripeRoughness, stripeRoughness, 1.0f});

    // Panel line grooves - rougher (dirt collects in crevices)
    float grooveRoughness = 0.85f;
    for (float y : {0.25f, 0.45f, 0.65f, 0.85f}) {
        canvas.rectFilled(0, h * y - 1, w, 3, {grooveRoughness, grooveRoughness, grooveRoughness, 1.0f});
    }
    for (float x : {0.2f, 0.4f, 0.6f, 0.8f}) {
        canvas.rectFilled(w * x - 1, 0, 3, h, {grooveRoughness, grooveRoughness, grooveRoughness, 1.0f});
    }

    // Weathering patches - random rough areas (wear and tear)
    float wornRoughness = 0.80f;
    for (int i = 0; i < 30; i++) {
        float wx = (float)((i * 89) % 100) / 100.0f * w;
        float wy = (float)((i * 151) % 100) / 100.0f * h;
        float size = 20 + (i % 4) * 15;
        canvas.circleFilled(wx, wy, size, {wornRoughness, wornRoughness, wornRoughness, 0.6f});
    }

    // Exhaust stains - very rough sooty areas at rear
    float sootRoughness = 0.95f;
    canvas.rectFilled(w * 0.4f, h * 0.85f, w * 0.2f, h * 0.15f, {sootRoughness, sootRoughness, sootRoughness, 0.7f});

    // Logo/decal areas - smoother (applied vinyl/paint)
    float decalRoughness = 0.45f;
    canvas.rectFilled(w * 0.1f, h * 0.1f, 80, 80, {decalRoughness, decalRoughness, decalRoughness, 1.0f});
    canvas.rectFilled(w * 0.8f, h * 0.1f, 80, 80, {decalRoughness, decalRoughness, decalRoughness, 1.0f});
}

// =============================================================================
// Livery Drawing Helper
// =============================================================================

void drawLivery(Canvas& canvas, const TeamPalette& team, bool fontLoaded) {
    const float w = 1024.0f;
    const float h = 1024.0f;

    // =========================================================================
    // Base Layer - Primary team color
    // =========================================================================
    canvas.clear(team.primary.r, team.primary.g, team.primary.b, 1.0f);

    // =========================================================================
    // Racing Stripes - Secondary color bands
    // =========================================================================

    // Main horizontal racing stripe (wider for more impact)
    canvas.rectFilled(0, h * 0.33f, w, h * 0.14f, team.secondary);

    // Thin accent stripe below main stripe
    canvas.rectFilled(0, h * 0.48f, w, h * 0.025f, team.accent);

    // Secondary stripe at top
    canvas.rectFilled(0, h * 0.05f, w, h * 0.04f, team.secondary);

    // =========================================================================
    // SPONSOR LOGOS - Geometric brand representations
    // =========================================================================

    // --- "AEGIS" Sponsor (top-left) - Shield shape ---
    float aegisX = w * 0.08f;
    float aegisY = h * 0.12f;
    // Shield outline (hexagon approximation)
    canvas.rectFilled(aegisX, aegisY, 70.0f, 45.0f, team.secondary);
    canvas.triangleFilled({aegisX, aegisY + 45.0f}, {aegisX + 70.0f, aegisY + 45.0f},
                          {aegisX + 35.0f, aegisY + 65.0f}, team.secondary);
    // Inner shield
    canvas.rectFilled(aegisX + 6, aegisY + 6, 58.0f, 35.0f, team.primary);
    canvas.triangleFilled({aegisX + 6, aegisY + 41.0f}, {aegisX + 64.0f, aegisY + 41.0f},
                          {aegisX + 35.0f, aegisY + 56.0f}, team.primary);
    // "A" letter mark (stylized)
    canvas.triangleFilled({aegisX + 35.0f, aegisY + 12.0f}, {aegisX + 22.0f, aegisY + 42.0f},
                          {aegisX + 48.0f, aegisY + 42.0f}, team.accent);
    canvas.rectFilled(aegisX + 28.0f, aegisY + 30.0f, 14.0f, 4.0f, team.primary);

    // --- "FUSION TECH" Sponsor (top-right) - Interlocking circles ---
    float fusionX = w * 0.72f;
    float fusionY = h * 0.12f;
    canvas.circleFilled(fusionX, fusionY + 20.0f, 22.0f, team.secondary, 16);
    canvas.circleFilled(fusionX + 28.0f, fusionY + 20.0f, 22.0f, team.secondary, 16);
    canvas.circleFilled(fusionX, fusionY + 20.0f, 14.0f, team.primary, 16);
    canvas.circleFilled(fusionX + 28.0f, fusionY + 20.0f, 14.0f, team.primary, 16);
    // Connecting bar
    canvas.rectFilled(fusionX - 5.0f, fusionY + 40.0f, 70.0f, 8.0f, team.accent);

    // --- "VELOCITY" Sponsor (mid-right) - Speed lines/arrow ---
    float velX = w * 0.78f;
    float velY = h * 0.54f;
    // Arrow shape pointing right
    canvas.triangleFilled({velX + 55.0f, velY + 18.0f}, {velX + 25.0f, velY},
                          {velX + 25.0f, velY + 36.0f}, team.secondary);
    // Speed lines
    for (int i = 0; i < 3; ++i) {
        canvas.rectFilled(velX, velY + 6.0f + i * 12.0f, 22.0f - i * 4.0f, 4.0f, team.secondary);
    }

    // --- "QUANTUM" Sponsor (mid-left) - Atom symbol ---
    float quantX = w * 0.12f;
    float quantY = h * 0.54f;
    // Central nucleus
    canvas.circleFilled(quantX + 20.0f, quantY + 20.0f, 8.0f, team.accent, 12);
    // Orbital ellipses (approximated with circles)
    canvas.circle(quantX + 20.0f, quantY + 20.0f, 20.0f, 2.0f, team.secondary, 16);
    canvas.circle(quantX + 20.0f, quantY + 20.0f, 28.0f, 1.5f, team.secondary.withAlpha(0.7f), 16);
    // Electron dots
    canvas.circleFilled(quantX + 40.0f, quantY + 20.0f, 3.0f, team.secondary, 8);
    canvas.circleFilled(quantX + 8.0f, quantY + 8.0f, 3.0f, team.secondary, 8);
    canvas.circleFilled(quantX + 8.0f, quantY + 32.0f, 3.0f, team.secondary, 8);

    // --- "AG-RACING LEAGUE" Badge (center stripe area) ---
    float badgeX = w * 0.5f;
    float badgeY = h * 0.395f;
    // Diamond shape
    canvas.triangleFilled({badgeX, badgeY - 20.0f}, {badgeX - 25.0f, badgeY},
                          {badgeX + 25.0f, badgeY}, team.accent);
    canvas.triangleFilled({badgeX, badgeY + 20.0f}, {badgeX - 25.0f, badgeY},
                          {badgeX + 25.0f, badgeY}, team.accent);
    // Inner diamond
    canvas.triangleFilled({badgeX, badgeY - 12.0f}, {badgeX - 15.0f, badgeY},
                          {badgeX + 15.0f, badgeY}, team.primary);
    canvas.triangleFilled({badgeX, badgeY + 12.0f}, {badgeX - 15.0f, badgeY},
                          {badgeX + 15.0f, badgeY}, team.primary);

    // =========================================================================
    // Chevron Patterns - Dynamic sponsor-style graphics
    // =========================================================================

    glm::vec4 chevronColor = team.secondary.withAlpha(0.8f);

    // Left side chevrons (larger, more prominent)
    for (int i = 0; i < 4; ++i) {
        float yBase = h * 0.55f + i * 22.0f;
        float indent = i * 8.0f;
        canvas.triangleFilled({0, yBase}, {70.0f - indent, yBase + 10.0f}, {0, yBase + 20.0f}, chevronColor);
    }

    // Right side chevrons (mirrored)
    for (int i = 0; i < 4; ++i) {
        float yBase = h * 0.55f + i * 22.0f;
        float indent = i * 8.0f;
        canvas.triangleFilled({w, yBase}, {w - 70.0f + indent, yBase + 10.0f}, {w, yBase + 20.0f}, chevronColor);
    }

    // Speed stripe decals (diagonal accent lines)
    for (int i = 0; i < 5; ++i) {
        float x = w * 0.68f + i * 12.0f;
        canvas.line(x, h * 0.15f, x + 35.0f, h * 0.30f, 3.0f, team.accent);
    }

    // Hazard stripes at rear (bottom edge)
    Color hazardDark = Color::fromHex("#1A1A1A");
    Color hazardBright = team.accent;
    float stripeW = 25.0f;
    for (int i = 0; i < 22; ++i) {
        glm::vec4 color = (i % 2 == 0) ? hazardDark : hazardBright;
        canvas.triangleFilled({i * stripeW, h}, {(i + 1) * stripeW, h},
                              {i * stripeW + stripeW * 0.5f, h - 22.0f}, color);
    }

    // =========================================================================
    // Panel Lines - Surface detail for mechanical look
    // =========================================================================

    Color panelLine = Color::Black.withAlpha(0.45f);
    Color panelLineLight = Color::White.withAlpha(0.12f);

    // Horizontal panel seams
    canvas.rectFilled(0, h * 0.20f, w, 2.5f, panelLine);
    canvas.rectFilled(0, h * 0.20f + 3.5f, w, 1.0f, panelLineLight);

    canvas.rectFilled(0, h * 0.52f, w, 2.5f, panelLine);
    canvas.rectFilled(0, h * 0.52f + 3.5f, w, 1.0f, panelLineLight);

    canvas.rectFilled(0, h * 0.72f, w, 2.5f, panelLine);
    canvas.rectFilled(0, h * 0.72f + 3.5f, w, 1.0f, panelLineLight);

    // Vertical panel seams
    canvas.rectFilled(w * 0.28f, 0, 2.0f, h * 0.33f, panelLine);
    canvas.rectFilled(w * 0.72f, 0, 2.0f, h * 0.33f, panelLine);
    canvas.rectFilled(w * 0.28f, h * 0.52f, 2.0f, h * 0.20f, panelLine);
    canvas.rectFilled(w * 0.72f, h * 0.52f, 2.0f, h * 0.20f, panelLine);

    // Diagonal panel seams (aerodynamic)
    canvas.line(w * 0.4f, 0, w * 0.3f, h * 0.20f, 2.0f, panelLine);
    canvas.line(w * 0.6f, 0, w * 0.7f, h * 0.20f, 2.0f, panelLine);

    // Rivet lines (small dots along panel seams)
    Color rivetColor = Color::DimGray.withAlpha(0.7f);
    for (int i = 0; i < 18; ++i) {
        float x = 15.0f + i * 28.0f;
        canvas.circleFilled(x, h * 0.20f - 5.0f, 2.0f, rivetColor, 6);
        canvas.circleFilled(x, h * 0.52f - 5.0f, 2.0f, rivetColor, 6);
    }

    // =========================================================================
    // Weathering & Grime - Edge darkening and wear marks
    // =========================================================================

    // Edge darkening (vignette-style grime on borders)
    Color grime = Color::Black.withAlpha(0.3f);
    Color grimeLight = Color::Black.withAlpha(0.15f);
    canvas.rectFilled(0, 0, w, 18.0f, grime);               // Top edge
    canvas.rectFilled(0, h - 30.0f, w, 30.0f, grime);       // Bottom edge (heavier)
    canvas.rectFilled(0, 0, 15.0f, h, grime);               // Left edge
    canvas.rectFilled(w - 15.0f, 0, 15.0f, h, grime);       // Right edge

    // Gradient grime from edges
    canvas.rectFilled(0, 18.0f, w, 10.0f, grimeLight);
    canvas.rectFilled(15.0f, 0, 10.0f, h, grimeLight);
    canvas.rectFilled(w - 25.0f, 0, 10.0f, h, grimeLight);

    // Exhaust staining (dark streaks near rear - more variation)
    Color exhaustStain = Color::Black.withAlpha(0.18f);
    Color exhaustStainLight = Color::Black.withAlpha(0.08f);
    for (int i = 0; i < 6; ++i) {
        float x = w * 0.15f + i * 65.0f;
        float streakHeight = h * 0.12f + (i % 2) * 0.05f * h;
        canvas.rectFilled(x, h * 0.88f, 6.0f + (i % 3) * 2.0f, streakHeight, exhaustStain);
        canvas.rectFilled(x + 8.0f, h * 0.90f, 4.0f, streakHeight * 0.7f, exhaustStainLight);
    }

    // Oil streaks (vertical smears)
    Color oilStreak = Color::Black.withAlpha(0.12f);
    canvas.rectFilled(w * 0.22f, h * 0.55f, 3.0f, h * 0.15f, oilStreak);
    canvas.rectFilled(w * 0.78f, h * 0.58f, 4.0f, h * 0.12f, oilStreak);
    canvas.rectFilled(w * 0.45f, h * 0.75f, 2.0f, h * 0.08f, oilStreak);

    // Random scratches (thin diagonal lines - more variety)
    Color scratch = Color::Black.withAlpha(0.22f);
    Color scratchLight = Color::White.withAlpha(0.08f);
    canvas.line(95, 175, 135, 205, 1.0f, scratch);
    canvas.line(97, 173, 137, 203, 1.0f, scratchLight);  // Highlight edge
    canvas.line(310, 85, 365, 115, 1.5f, scratch);
    canvas.line(415, 275, 450, 315, 1.0f, scratch);
    canvas.line(75, 395, 115, 440, 1.0f, scratch);
    canvas.line(375, 375, 405, 425, 1.5f, scratch);
    canvas.line(200, 420, 240, 445, 1.0f, scratch);
    canvas.line(280, 140, 310, 165, 1.0f, scratch);

    // Scuff marks (small semi-transparent rectangles - more)
    Color scuff = Color::Black.withAlpha(0.14f);
    canvas.rectFilled(145, 295, 28, 10, scuff);
    canvas.rectFilled(345, 145, 22, 12, scuff);
    canvas.rectFilled(65, 445, 35, 14, scuff);
    canvas.rectFilled(425, 345, 25, 11, scuff);
    canvas.rectFilled(180, 180, 18, 8, scuff);
    canvas.rectFilled(390, 220, 20, 9, scuff);
    canvas.rectFilled(250, 380, 24, 10, scuff);

    // Edge chipping (small notches on panel lines)
    Color chip = Color::Black.withAlpha(0.35f);
    canvas.rectFilled(w * 0.35f, h * 0.20f - 3.0f, 8.0f, 6.0f, chip);
    canvas.rectFilled(w * 0.58f, h * 0.20f - 2.0f, 6.0f, 5.0f, chip);
    canvas.rectFilled(w * 0.42f, h * 0.52f - 3.0f, 7.0f, 6.0f, chip);
    canvas.rectFilled(w * 0.65f, h * 0.72f - 2.0f, 9.0f, 5.0f, chip);

    // =========================================================================
    // Team Number - Bold racing number in circle
    // =========================================================================

    // Always draw the circle background (don't require font)
    canvas.circleFilled(w * 0.5f, h * 0.80f, 52.0f, team.secondary, 32);
    canvas.circle(w * 0.5f, h * 0.80f, 52.0f, 5.0f, team.accent, 32);
    canvas.circle(w * 0.5f, h * 0.80f, 44.0f, 2.0f, team.primary, 32);

    if (fontLoaded) {
        // Team number text - uses team primary color on white circle
        canvas.textCentered(team.number, w * 0.5f, h * 0.80f, team.primary);
    }

    // =========================================================================
    // Accent Blocks - Side pod color blocks (larger)
    // =========================================================================

    canvas.rectFilled(0, h * 0.58f, w * 0.10f, h * 0.14f, team.accent);
    canvas.rectFilled(w * 0.90f, h * 0.58f, w * 0.10f, h * 0.14f, team.accent);

    // Inner accent detail
    canvas.rectFilled(w * 0.02f, h * 0.60f, w * 0.04f, h * 0.10f, team.secondary);
    canvas.rectFilled(w * 0.94f, h * 0.60f, w * 0.04f, h * 0.10f, team.secondary);
}

// =============================================================================
// Setup
// =============================================================================

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Livery Texture - Canvas-based procedural texture
    // =========================================================================

    auto& livery = chain.add<Canvas>("livery").size(1024, 1024);

    // Load font for livery text (team numbers)
    // Using space age.ttf which has digit glyphs
    if (livery.loadFont(ctx, "assets/fonts/space age.ttf", 64.0f)) {
        g_fontLoaded = true;
    } else {
        std::cout << "Warning: Could not load livery font" << std::endl;
    }

    // =========================================================================
    // Grime Texture Overlay - Adds weathering and wear to livery
    // =========================================================================

    auto& grimeTexture = chain.add<Image>("grime")
        .file("examples/showcase/wipeout-viz/assets/textures/grime/DarkGrunge_Textures01.jpg");

    // Composite grime over livery using Overlay blend for realistic weathering
    auto& liveryWithGrime = chain.add<Composite>("liveryGrime")
        .input(0, &livery)
        .input(1, &grimeTexture)
        .mode(BlendMode::Overlay)
        .opacity(0.7f);  // More visible weathering

    // =========================================================================
    // Normal Map - Procedural surface detail
    // =========================================================================

    auto& normalMap = chain.add<Canvas>("normalMap").size(1024, 1024);

    // =========================================================================
    // Roughness Map - Procedural surface variation
    // =========================================================================

    auto& roughnessMap = chain.add<Canvas>("roughnessMap").size(1024, 1024);

    // =========================================================================
    // Craft Material - Full PBR with weathered livery
    // =========================================================================

    auto& material = chain.add<TexturedMaterial>("material")
        .baseColorInput(&liveryWithGrime)
        .normalInput(&normalMap)
        .roughnessInput(&roughnessMap)
        .normalScale(1.0f)
        .roughnessFactor(1.0f)  // Use texture values directly
        .metallicFactor(0.15f);

    // =========================================================================
    // Craft Geometry - Modular Class-based Design
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Toggle to study reference models instead of procedural craft
    static bool g_useReferenceModel = false;  // Set to true to load GLTF reference
    static const char* g_referenceModelPath = "examples/showcase/wipeout-viz/assets/models/wipeout_-_goteki/scene.gltf";

    if (g_useReferenceModel) {
        // Load reference model for study
        auto& refModel = chain.add<GLTFLoader>("refModel")
            .file(g_referenceModelPath)
            .loadTextures(true)
            .scale(15.0f);  // These models are tiny, scale up
        // Rotate to align (Z forward -> X forward)
        glm::mat4 modelT = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), {0, 1, 0});
        scene.add(&refModel, modelT);
        std::cout << "Loaded reference model: " << g_referenceModelPath << std::endl;
    } else {
        // Build the aerodynamic craft mesh and add with textured material
        auto craftMesh = g_craft.build();
        auto& craftStatic = chain.add<StaticMesh>("craftMesh");
        craftStatic.mesh(craftMesh);
        scene.add(&craftStatic, &material);
    }

    // =========================================================================
    // Engine Glow - Emissive Material
    // =========================================================================

    // Create a solid orange canvas for emissive texture (workaround for default black)
    auto& glowTex = chain.add<Canvas>("glowTex").size(64, 64);

    auto& glowMaterial = chain.add<TexturedMaterial>("glowMaterial")
        .baseColorFactor(1.0f, 0.7f, 0.2f, 1.0f)   // Bright orange-yellow base
        .emissiveInput(&glowTex)                    // Use canvas as emissive texture
        .emissiveFactor(1.0f, 0.7f, 0.2f)          // Bright orange-yellow
        .emissiveStrength(50.0f)                    // Very high emission for bloom
        .metallicFactor(0.0f)
        .roughnessFactor(1.0f);

    // Build engine glow mesh and add with emissive material
    auto glowMesh = g_craft.buildEngineGlow();
    auto& glowStatic = chain.add<StaticMesh>("glowMesh");
    glowStatic.mesh(glowMesh);
    scene.add(&glowStatic, &glowMaterial);

    // =========================================================================
    // Grid Floor - Tron-style ground plane
    // =========================================================================

    auto& gridCanvas = chain.add<Canvas>("gridCanvas").size(512, 512);

    auto& gridMaterial = chain.add<TexturedMaterial>("gridMaterial")
        .baseColorInput(&gridCanvas)
        .emissiveInput(&gridCanvas)      // Self-lit grid lines
        .emissiveStrength(1.5f)
        .metallicFactor(0.0f)
        .roughnessFactor(1.0f)
        .doubleSided(true);

    // Large floor plane below the craft
    glm::mat4 floorT = glm::translate(glm::mat4(1.0f), glm::vec3(0, -1.0f, 0));
    floorT = glm::scale(floorT, glm::vec3(20.0f, 1.0f, 20.0f));  // Large floor
    auto& floor = scene.add<Plane>("floor", floorT);
    floor.size(1.0f, 1.0f);
    scene.setMaterial(scene.entries().size() - 1, &gridMaterial);

    // =========================================================================
    // Audio - Synthetic engine sound + analysis for reactivity
    // =========================================================================

    // Low frequency drone for bass reactivity
    auto& bassDrone = chain.add<Oscillator>("bassDrone")
        .frequency(55.0f)      // Low A
        .waveform(Waveform::Sine)
        .volume(0.3f);

    // Mid frequency buzz for mids reactivity
    auto& midBuzz = chain.add<Oscillator>("midBuzz")
        .frequency(220.0f)     // A below middle C
        .waveform(Waveform::Saw)
        .volume(0.2f);

    // Mix the oscillators
    auto& audioMix = chain.add<AudioMixer>("audioMix")
        .input(0, "bassDrone")
        .input(1, "midBuzz")
        .gain(0, 1.0f)
        .gain(1, 0.6f);

    // Frequency band analysis for audio-reactive visuals
    auto& bands = chain.add<BandSplit>("bands")
        .input("audioMix")
        .smoothing(0.85f)
        .fftSize(1024);

    // Audio output for speakers (vivid::AudioOutput is in core)
    auto& audioOut = chain.add<vivid::AudioOutput>("audioOut")
        .input("audioMix")
        .volume(0.3f);  // Keep it quiet

    chain.audioOutput("audioOut");

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
        .intensity(2.5f);  // Brighter for evaluation

    auto& fillLight = chain.add<DirectionalLight>("fillLight")
        .direction(-1.0f, 0.5f, -1.0f)
        .color(0.6f, 0.7f, 1.0f)  // Cool blue fill
        .intensity(1.0f);  // Stronger fill

    // =========================================================================
    // 3D Render
    // =========================================================================

    auto& render = chain.add<Render3D>("render")
        .input(&scene)
        .cameraInput(&camera)
        .lightInput(&keyLight)
        .addLight(&fillLight)
        .material(&material)
        .shadingMode(ShadingMode::PBR)
        .ambient(0.3f)  // Higher ambient for visibility
        .clearColor(0.02f, 0.02f, 0.05f)
        .resolution(1280, 720);

    // =========================================================================
    // Retro Post-Processing Pipeline
    // =========================================================================

    // Bloom for engine glow (before downsample to preserve detail)
    // Note: Render output is clamped to [0,1], so we need low threshold
    auto& bloom = chain.add<Bloom>("bloom")
        .input(&render)
        .threshold(0.3f)      // Low threshold to catch orange glow (luminance ~0.73)
        .intensity(4.0f)      // Strong bloom to compensate for clamped HDR
        .radius(40.0f)        // Wide glow for halo effect
        .passes(4);           // More passes for smoother blur

    // Downsample to low resolution (PS1-style)
    auto& downsample = chain.add<Downsample>("downsample")
        .input(&bloom)        // Now takes bloom output
        .resolution(480, 270)
        .filter(FilterMode::Nearest);

    // Dither for color banding
    auto& dither = chain.add<Dither>("dither")
        .input(&downsample)
        .pattern(DitherPattern::Bayer4x4)
        .levels(32)
        .strength(0.7f);

    // =========================================================================
    // Composable CRT Effects (replaces monolithic CRTEffect)
    // =========================================================================

    // Scanlines - alternating dark lines
    auto& scanlines = chain.add<Scanlines>("scanlines")
        .input(&dither)
        .spacing(3)
        .thickness(0.4f)
        .intensity(0.15f);

    // Barrel distortion - curved CRT screen
    auto& barrel = chain.add<BarrelDistortion>("barrel")
        .input(&scanlines)
        .curvature(0.08f);

    // Chromatic aberration - color fringing
    auto& chroma = chain.add<ChromaticAberration>("chroma")
        .input(&barrel)
        .amount(0.015f)
        .radial(false);  // Linear horizontal separation like original CRT

    // Vignette - edge darkening
    auto& vignette = chain.add<Vignette>("vignette")
        .input(&chroma)
        .intensity(0.3f)
        .softness(0.5f);

    // =========================================================================
    // UI Overlay - Team name and shading mode
    // =========================================================================

    auto& ui = chain.add<Canvas>("ui").size(1280, 720);

    // Load font for UI text (smaller size)
    if (!ui.loadFont(ctx, "assets/fonts/space age.ttf", 24.0f)) {
        std::cout << "Warning: Could not load UI font" << std::endl;
    }

    // Composite UI over post-processed render
    auto& composite = chain.add<Composite>("composite")
        .input(0, &vignette)
        .input(1, &ui)
        .mode(BlendMode::Over);

    chain.output("composite");  // Full render with UI

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

    // Draw PBR maps
    auto& normalMap = chain.get<Canvas>("normalMap");
    drawNormalMap(normalMap);

    auto& roughnessMap = chain.get<Canvas>("roughnessMap");
    drawRoughnessMap(roughnessMap, g_teams[g_currentTeam]);

    auto& gridCanvas = chain.get<Canvas>("gridCanvas");
    drawGridFloor(gridCanvas, g_teams[g_currentTeam], time);

    // Draw glow texture - solid white for emissive (color comes from emissiveFactor)
    auto& glowTex = chain.get<Canvas>("glowTex");
    glowTex.clear(1.0f, 1.0f, 1.0f, 1.0f);  // Solid white - emissiveFactor controls color

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
    // Audio-Reactive Visuals
    // =========================================================================

    // Get frequency band analysis
    auto& bands = chain.get<BandSplit>("bands");
    float bassLevel = bands.bass();      // 60-250 Hz
    float midLevel = bands.mid();        // 500-2000 Hz

    // Engine glow reacts to mids
    auto& glowMaterial = chain.get<TexturedMaterial>("glowMaterial");
    float glowIntensity = 5.0f + midLevel * 15.0f;  // 5.0 base + up to 20.0 with audio
    glowMaterial.emissiveStrength(glowIntensity);

    // Modulate oscillator frequencies for variation
    auto& bassDrone = chain.get<Oscillator>("bassDrone");
    auto& midBuzz = chain.get<Oscillator>("midBuzz");
    float freqMod = sin(time * 0.3f);
    bassDrone.frequency(50.0f + freqMod * 10.0f);      // Wobble 40-60 Hz
    midBuzz.frequency(200.0f + freqMod * 40.0f);       // Wobble 160-240 Hz

    // =========================================================================
    // Hover Animation - apply to entire scene
    // =========================================================================

    // Bass modulates hover amplitude
    float hoverAmp = 0.03f + bassLevel * 0.08f;  // Base + audio-reactive boost
    float hover = sin(time * 1.5f) * hoverAmp;
    float roll = sin(time * 0.7f) * (0.02f + bassLevel * 0.03f);

    glm::mat4 hoverMat = glm::translate(glm::mat4(1.0f), glm::vec3(0, hover, 0));
    hoverMat = glm::rotate(hoverMat, roll, glm::vec3(1, 0, 0));

    auto& scene = chain.get<SceneComposer>("scene");
    scene.rootTransform(hoverMat);
}

VIVID_CHAIN(setup, update)
