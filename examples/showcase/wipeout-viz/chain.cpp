// Wipeout 2029 - Procedural Anti-Gravity Craft Showcase
// Phase 1: Minimal craft with retro post-processing
//
// Demonstrates: MeshBuilder, SceneComposer, Render3D, Downsample, Dither, CRTEffect
//
// Controls:
//   Mouse drag: Orbit camera
//   V: Toggle VertexLit/PBR shading
//   TAB: Open parameter controls

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Team color palette (FEISAR blue)
static glm::vec4 g_primaryColor = {0.17f, 0.36f, 0.69f, 1.0f};   // #2B5CAF
static glm::vec4 g_secondaryColor = {1.0f, 1.0f, 1.0f, 1.0f};    // White

// Shading toggle
static bool g_useVertexLit = true;

// Camera orbit state
static float g_cameraYaw = 0.0f;
static float g_cameraPitch = 0.3f;
static float g_cameraDistance = 5.0f;
static glm::vec2 g_lastMouse = {0, 0};

// =============================================================================
// Craft Geometry Generation
// =============================================================================

MeshBuilder createCraft() {
    // ==========================================================================
    // Aerodynamic Anti-Gravity Craft
    // Inspired by Wipeout 2097 FEISAR - sleek, tapered, faceted
    // Uses pyramid, wedge, frustum primitives for angular shapes
    // ==========================================================================

    // --- Main Fuselage ---
    // Central body section
    auto fuselage = MeshBuilder::box(1.4f, 0.16f, 0.32f);

    // Angular nose - 4-sided pyramid pointing forward
    auto nose = MeshBuilder::pyramid(0.32f, 0.7f, 4)
        .rotate(glm::radians(-90.0f), {0, 0, 1})   // Point forward (+X)
        .translate({1.05f, 0, 0});
    fuselage.append(nose);

    // Rear taper - frustum narrowing toward back
    auto tail = MeshBuilder::frustum(0.16f, 0.08f, 0.35f, 6)
        .rotate(glm::radians(90.0f), {0, 0, 1})    // Point backward (-X)
        .translate({-0.88f, 0, 0});
    fuselage.append(tail);

    // --- Cockpit Canopy ---
    // Angular canopy using wedge for sloped front
    auto cockpitBase = MeshBuilder::box(0.35f, 0.08f, 0.2f)
        .translate({0.2f, 0.12f, 0});
    fuselage.append(cockpitBase);

    // Sloped windscreen (wedge rotated to slope backward)
    auto windscreen = MeshBuilder::wedge(0.2f, 0.1f, 0.18f)
        .rotate(glm::radians(180.0f), {0, 1, 0})   // Flip so slope faces forward
        .translate({0.42f, 0.12f, 0});
    fuselage.append(windscreen);

    // --- Side Pods (Engine Nacelles) ---
    // Main pod body - frustum for tapered look
    auto leftPod = MeshBuilder::frustum(0.11f, 0.09f, 1.1f, 8)
        .rotate(glm::radians(90.0f), {0, 0, 1})
        .translate({-0.05f, -0.03f, 0.4f});

    // Pod nose - pyramid for sharp angular front
    auto leftPodNose = MeshBuilder::pyramid(0.18f, 0.3f, 4)
        .rotate(glm::radians(-90.0f), {0, 0, 1})
        .translate({0.55f, -0.03f, 0.4f});
    leftPod.append(leftPodNose);

    // Engine exhaust - frustum opening up at rear
    auto leftExhaust = MeshBuilder::frustum(0.07f, 0.1f, 0.15f, 6)
        .rotate(glm::radians(90.0f), {0, 0, 1})
        .translate({-0.68f, -0.03f, 0.4f});
    leftPod.append(leftExhaust);

    fuselage.append(leftPod);

    // Right pod (mirror of left)
    auto rightPod = MeshBuilder::frustum(0.11f, 0.09f, 1.1f, 8)
        .rotate(glm::radians(90.0f), {0, 0, 1})
        .translate({-0.05f, -0.03f, -0.4f});

    auto rightPodNose = MeshBuilder::pyramid(0.18f, 0.3f, 4)
        .rotate(glm::radians(-90.0f), {0, 0, 1})
        .translate({0.55f, -0.03f, -0.4f});
    rightPod.append(rightPodNose);

    auto rightExhaust = MeshBuilder::frustum(0.07f, 0.1f, 0.15f, 6)
        .rotate(glm::radians(90.0f), {0, 0, 1})
        .translate({-0.68f, -0.03f, -0.4f});
    rightPod.append(rightExhaust);

    fuselage.append(rightPod);

    // --- Connecting Struts ---
    // Wedge-shaped struts for aerodynamic look
    auto leftStrut = MeshBuilder::wedge(0.25f, 0.06f, 0.12f)
        .rotate(glm::radians(90.0f), {0, 1, 0})    // Rotate so ramp faces outward
        .rotate(glm::radians(-5.0f), {1, 0, 0})    // Slight downward angle
        .translate({0.1f, 0, 0.26f});
    auto rightStrut = MeshBuilder::wedge(0.25f, 0.06f, 0.12f)
        .rotate(glm::radians(-90.0f), {0, 1, 0})
        .rotate(glm::radians(5.0f), {1, 0, 0})
        .translate({0.1f, 0, -0.26f});
    fuselage.append(leftStrut);
    fuselage.append(rightStrut);

    // --- Rear Wing ---
    auto wing = MeshBuilder::box(0.22f, 0.018f, 0.9f)
        .translate({-0.75f, 0.18f, 0});

    // Wing endplates - wedge-shaped for swept look
    auto leftEndplate = MeshBuilder::wedge(0.2f, 0.14f, 0.02f)
        .rotate(glm::radians(-90.0f), {0, 0, 1})   // Vertical, ramp at top
        .translate({-0.75f, 0.2f, 0.45f});
    auto rightEndplate = MeshBuilder::wedge(0.2f, 0.14f, 0.02f)
        .rotate(glm::radians(-90.0f), {0, 0, 1})
        .translate({-0.75f, 0.2f, -0.45f});
    wing.append(leftEndplate);
    wing.append(rightEndplate);

    fuselage.append(wing);

    // --- Vertical Fin ---
    // Pyramid-based fin for aggressive angular look
    auto fin = MeshBuilder::pyramid(0.04f, 0.32f, 4)
        .scale({4.0f, 1.0f, 1.0f})                 // Stretch into blade shape
        .rotate(glm::radians(15.0f), {0, 0, 1})    // Lean backward
        .translate({-0.65f, 0.28f, 0});
    fuselage.append(fin);

    // --- Front Canards ---
    // Small delta wings near nose
    auto leftCanard = MeshBuilder::pyramid(0.15f, 0.02f, 3)  // Triangle base
        .scale({1.5f, 1.0f, 1.0f})
        .rotate(glm::radians(-90.0f), {1, 0, 0})   // Lay flat
        .rotate(glm::radians(-20.0f), {0, 1, 0})   // Sweep back
        .translate({0.65f, 0.02f, 0.22f});
    auto rightCanard = MeshBuilder::pyramid(0.15f, 0.02f, 3)
        .scale({1.5f, 1.0f, 1.0f})
        .rotate(glm::radians(-90.0f), {1, 0, 0})
        .rotate(glm::radians(20.0f), {0, 1, 0})
        .translate({0.65f, 0.02f, -0.22f});
    fuselage.append(leftCanard);
    fuselage.append(rightCanard);

    // --- Air Intakes ---
    // Wedge scoops on fuselage top
    auto leftIntake = MeshBuilder::wedge(0.12f, 0.05f, 0.06f)
        .translate({-0.15f, 0.08f, 0.1f});
    auto rightIntake = MeshBuilder::wedge(0.12f, 0.05f, 0.06f)
        .translate({-0.15f, 0.08f, -0.1f});
    fuselage.append(leftIntake);
    fuselage.append(rightIntake);

    // Apply flat normals for faceted PS1 look
    fuselage.computeFlatNormals();

    return fuselage;
}

// =============================================================================
// Setup
// =============================================================================

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Craft Geometry - Procedural MeshBuilder
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Build the aerodynamic craft mesh
    auto craftMesh = createCraft();
    scene.addMesh("craft", craftMesh, glm::mat4(1.0f), g_primaryColor);

    // =========================================================================
    // Camera and Lighting
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera")
        .position(0, 1.5f, -5.0f)
        .target(0, 0, 0)
        .fov(45.0f);

    auto& keyLight = chain.add<DirectionalLight>("keyLight")
        .direction(1.0f, 2.0f, 1.0f)
        .color(1.0f, 0.95f, 0.9f)
        .intensity(1.5f);

    auto& fillLight = chain.add<DirectionalLight>("fillLight")
        .direction(-1.0f, 0.5f, -1.0f)
        .color(0.6f, 0.7f, 1.0f)
        .intensity(0.4f);

    // =========================================================================
    // 3D Render
    // =========================================================================

    auto& render = chain.add<Render3D>("render")
        .input(&scene)
        .cameraInput(&camera)
        .lightInput(&keyLight)
        .lightInput(&fillLight)
        .shadingMode(ShadingMode::VertexLit)
        .clearColor(0.02f, 0.02f, 0.05f);

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

    chain.output("crt");
}

// =============================================================================
// Update
// =============================================================================

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // =========================================================================
    // Input: Toggle shading mode
    // =========================================================================

    if (ctx.key(GLFW_KEY_V).pressed) {
        g_useVertexLit = !g_useVertexLit;
        auto& render = chain.get<Render3D>("render");
        render.shadingMode(g_useVertexLit ? ShadingMode::VertexLit : ShadingMode::PBR);
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
