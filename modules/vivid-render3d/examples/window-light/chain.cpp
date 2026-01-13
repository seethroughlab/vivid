// Window Light - Vivid Example
// Volumetric lighting with directional light shadows through a window
//
// Demonstrates:
// - DirectionalLight with shadow casting
// - Shadow-occluded volumetric lighting
// - God rays streaming through window opening
// - Interior scene with furniture casting shadows

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/gui/imgui.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Volumetric controls
static float g_density = 0.15f;         // Higher for visible shafts
static float g_intensity = 2.0f;
static float g_maxDistance = 30.0f;
static float g_anisotropy = 0.8f;       // Strong forward scatter
static int g_raySteps = 64;
static float g_fogColor[3] = {0.03f, 0.035f, 0.04f};  // Warm dust color

// Shadow controls
static bool g_useShadows = true;
static float g_shadowBias = 0.003f;
static float g_shadowStrength = 1.0f;

// Light controls
static float g_lightIntensity = 3.0f;
static float g_lightDir[3] = {0.3f, -0.6f, -0.7f};  // Light from behind window toward camera
static float g_lightColor[3] = {1.0f, 0.95f, 0.85f};  // Warm sunlight

// Camera controls
static bool g_autoOrbit = false;
static float g_orbitSpeed = 0.1f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // SCENE - Dark room with window and furniture
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Room dimensions
    const float roomWidth = 10.0f;
    const float roomHeight = 6.0f;
    const float roomDepth = 12.0f;
    const float wallThickness = 0.3f;

    // Floor - dark wood
    auto& floor = scene.add<Box>("floor",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.15f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(roomWidth, wallThickness, roomDepth)),
        glm::vec4(0.15f, 0.1f, 0.08f, 1.0f));

    // Back wall (with window opening)
    // Left portion of back wall
    auto& backWallLeft = scene.add<Box>("backWallLeft",
        glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, roomHeight / 2.0f, -roomDepth / 2.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(4.0f, roomHeight, wallThickness)),
        glm::vec4(0.25f, 0.22f, 0.2f, 1.0f));

    // Right portion of back wall
    auto& backWallRight = scene.add<Box>("backWallRight",
        glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, roomHeight / 2.0f, -roomDepth / 2.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(4.0f, roomHeight, wallThickness)),
        glm::vec4(0.25f, 0.22f, 0.2f, 1.0f));

    // Top portion above window
    auto& backWallTop = scene.add<Box>("backWallTop",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, roomHeight - 0.75f, -roomDepth / 2.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.5f, wallThickness)),
        glm::vec4(0.25f, 0.22f, 0.2f, 1.0f));

    // Bottom portion below window
    auto& backWallBottom = scene.add<Box>("backWallBottom",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.75f, -roomDepth / 2.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.5f, wallThickness)),
        glm::vec4(0.25f, 0.22f, 0.2f, 1.0f));

    // Window frame pieces (thin dark wood)
    glm::vec4 frameColor(0.1f, 0.08f, 0.06f, 1.0f);

    // Vertical divider
    auto& windowDividerV = scene.add<Box>("windowDividerV",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, roomHeight / 2.0f, -roomDepth / 2.0f + 0.05f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.08f, 3.0f, 0.1f)),
        frameColor);

    // Horizontal divider
    auto& windowDividerH = scene.add<Box>("windowDividerH",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, roomHeight / 2.0f, -roomDepth / 2.0f + 0.05f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 0.08f, 0.1f)),
        frameColor);

    // Window blinds/slats - create distinct shadow bands in volumetric fog
    const int NUM_BLINDS = 8;
    for (int i = 0; i < NUM_BLINDS; i++) {
        float yPos = 1.5f + static_cast<float>(i) * 0.4f;  // Spread across window height
        std::string blindName = "blind" + std::to_string(i);

        auto& blind = scene.add<Box>(blindName,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, yPos, -roomDepth / 2.0f + 0.08f)) *
            glm::rotate(glm::mat4(1.0f), glm::radians(15.0f), glm::vec3(1, 0, 0)) *  // Angled
            glm::scale(glm::mat4(1.0f), glm::vec3(1.8f, 0.04f, 0.15f)),
            glm::vec4(0.9f, 0.88f, 0.85f, 1.0f));  // Light colored blinds
    }

    // Left wall
    auto& leftWall = scene.add<Box>("leftWall",
        glm::translate(glm::mat4(1.0f), glm::vec3(-roomWidth / 2.0f, roomHeight / 2.0f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(wallThickness, roomHeight, roomDepth)),
        glm::vec4(0.22f, 0.2f, 0.18f, 1.0f));

    // Right wall
    auto& rightWall = scene.add<Box>("rightWall",
        glm::translate(glm::mat4(1.0f), glm::vec3(roomWidth / 2.0f, roomHeight / 2.0f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(wallThickness, roomHeight, roomDepth)),
        glm::vec4(0.22f, 0.2f, 0.18f, 1.0f));

    // =========================================================================
    // FURNITURE - Objects to cast shadows into the light beams
    // =========================================================================

    // Table
    auto& tableTop = scene.add<Box>("tableTop",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -2.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(2.5f, 0.1f, 1.2f)),
        glm::vec4(0.3f, 0.2f, 0.12f, 1.0f));

    // Table legs
    for (int i = 0; i < 4; i++) {
        float xOff = (i % 2 == 0) ? -1.1f : 1.1f;
        float zOff = (i < 2) ? -0.5f : 0.5f;
        std::string legName = "tableLeg" + std::to_string(i);

        auto& leg = scene.add<Cylinder>(legName,
            glm::translate(glm::mat4(1.0f), glm::vec3(xOff, 0.7f, -2.0f + zOff)),
            glm::vec4(0.25f, 0.15f, 0.1f, 1.0f));
        leg.radius(0.06f);
        leg.height(1.4f);
        leg.segments(8);
    }

    // Vase on table
    auto& vase = scene.add<Cylinder>("vase",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 1.8f, -2.0f)),
        glm::vec4(0.4f, 0.35f, 0.5f, 1.0f));
    vase.radius(0.15f);
    vase.height(0.5f);
    vase.segments(12);

    // Chair
    auto& chairSeat = scene.add<Box>("chairSeat",
        glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.9f, -0.5f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(0, 1, 0)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.8f, 0.08f, 0.8f)),
        glm::vec4(0.28f, 0.18f, 0.1f, 1.0f));

    auto& chairBack = scene.add<Box>("chairBack",
        glm::translate(glm::mat4(1.0f), glm::vec3(-1.8f, 1.6f, -0.8f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(0, 1, 0)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.08f, 1.2f, 0.8f)),
        glm::vec4(0.28f, 0.18f, 0.1f, 1.0f));

    // Standing lamp
    auto& lampPole = scene.add<Cylinder>("lampPole",
        glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 1.5f, -3.5f)),
        glm::vec4(0.15f, 0.12f, 0.1f, 1.0f));
    lampPole.radius(0.04f);
    lampPole.height(3.0f);
    lampPole.segments(8);

    // Dust particles in the air (small spheres scattered in light path)
    // These will catch the volumetric lighting nicely
    for (int i = 0; i < 5; i++) {
        float x = -0.5f + static_cast<float>(i % 3) * 0.5f;
        float y = 2.0f + static_cast<float>(i) * 0.4f;
        float z = -3.0f + static_cast<float>(i % 2) * 0.8f;
        std::string dustName = "dust" + std::to_string(i);

        auto& dust = scene.add<Sphere>(dustName,
            glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)),
            glm::vec4(0.5f, 0.5f, 0.5f, 0.3f));
        dust.radius(0.02f);
        dust.segments(6);
    }

    // =========================================================================
    // CAMERA - Inside room looking toward window
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(0.0f, 2.5f, 4.0f);
    camera.target(0.0f, 2.5f, -6.0f);
    camera.fov(60.0f);
    camera.nearPlane(0.1f);
    camera.farPlane(50.0f);

    // =========================================================================
    // LIGHTING - Directional sunlight through window
    // =========================================================================

    // Main sunlight - directional, casting through window
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(g_lightDir[0], g_lightDir[1], g_lightDir[2]);
    sun.color(g_lightColor[0], g_lightColor[1], g_lightColor[2]);
    sun.intensity = g_lightIntensity;
    sun.castShadow(true);
    sun.shadowBias(0.002f);

    // Very dim ambient (interior darkness)
    auto& ambient = chain.add<DirectionalLight>("ambient");
    ambient.direction(0, -1, 0);
    ambient.color(0.05f, 0.05f, 0.06f);
    ambient.intensity = 0.05f;  // Much dimmer

    // =========================================================================
    // RENDER3D - With shadows for volumetric occlusion
    // =========================================================================

    auto& render = chain.add<Render3D>("render3d");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sun);
    render.addLight(&ambient);
    render.setShadingMode(ShadingMode::Flat);  // Flat for more contrast
    render.setAmbient(0.01f);                   // Very dark
    render.setClearColor(0.005f, 0.005f, 0.008f);  // Near black
    render.setResolution(1920, 1080);
    render.setDepthOutput(true);
    render.setShadows(true);
    render.setShadowMapResolution(2048);

    // =========================================================================
    // VOLUMETRIC LIGHTING - God rays with shadow occlusion
    // =========================================================================

    auto& volumetric = chain.add<VolumetricLighting>("volumetric");
    volumetric.input(&render);
    volumetric.lightInput(&sun);
    volumetric.cameraInput(&camera);

    // Dusty atmosphere parameters
    volumetric.density = g_density;
    volumetric.intensity = g_intensity;
    volumetric.maxDistance = g_maxDistance;
    volumetric.anisotropy = g_anisotropy;
    volumetric.raySteps = g_raySteps;
    volumetric.fogColor[0] = g_fogColor[0];
    volumetric.fogColor[1] = g_fogColor[1];
    volumetric.fogColor[2] = g_fogColor[2];

    // Shadow occlusion - key feature for window light shafts
    volumetric.useShadows = g_useShadows;
    volumetric.shadowBias = g_shadowBias;
    volumetric.shadowStrength = g_shadowStrength;

    chain.output("volumetric");

    // Initialize ImGui
    vivid::imgui::init(ctx);

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // TAB toggles ImGui
    if (ctx.key(GLFW_KEY_TAB).pressed) {
        vivid::imgui::toggleVisible();
    }

    // ImGui controls
    if (vivid::imgui::isVisible()) {
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340, 500), ImGuiCond_FirstUseEver);
        ImGui::Begin("Window Light");

        ImGui::SeparatorText("Volumetric Lighting");
        ImGui::SliderFloat("Density", &g_density, 0.0f, 0.2f);
        ImGui::SliderFloat("Intensity", &g_intensity, 0.0f, 5.0f);
        ImGui::SliderFloat("Max Distance", &g_maxDistance, 5.0f, 50.0f);
        ImGui::SliderFloat("Anisotropy", &g_anisotropy, -1.0f, 1.0f);
        ImGui::SliderInt("Ray Steps", &g_raySteps, 16, 128);
        ImGui::ColorEdit3("Fog Color", g_fogColor);

        ImGui::SeparatorText("Shadow Occlusion");
        ImGui::Checkbox("Use Shadows", &g_useShadows);
        ImGui::SliderFloat("Shadow Bias", &g_shadowBias, 0.0f, 0.02f, "%.4f");
        ImGui::SliderFloat("Shadow Strength", &g_shadowStrength, 0.0f, 1.0f);

        ImGui::SeparatorText("Directional Light");
        ImGui::SliderFloat("Light Intensity", &g_lightIntensity, 0.0f, 5.0f);
        ImGui::SliderFloat3("Light Direction", g_lightDir, -1.0f, 1.0f);
        ImGui::ColorEdit3("Light Color", g_lightColor);

        ImGui::SeparatorText("Camera");
        ImGui::Checkbox("Auto Orbit", &g_autoOrbit);
        ImGui::SliderFloat("Orbit Speed", &g_orbitSpeed, 0.0f, 0.5f);

        ImGui::Separator();
        ImGui::Text("Press TAB to hide panel");

        ImGui::End();
    }

    // Update directional light
    auto& sun = chain.get<DirectionalLight>("sun");
    sun.direction(g_lightDir[0], g_lightDir[1], g_lightDir[2]);
    sun.color(g_lightColor[0], g_lightColor[1], g_lightColor[2]);
    sun.intensity = g_lightIntensity;

    // Update volumetric parameters
    auto& volumetric = chain.get<VolumetricLighting>("volumetric");
    volumetric.density = g_density;
    volumetric.intensity = g_intensity;
    volumetric.maxDistance = g_maxDistance;
    volumetric.anisotropy = g_anisotropy;
    volumetric.raySteps = g_raySteps;
    volumetric.fogColor[0] = g_fogColor[0];
    volumetric.fogColor[1] = g_fogColor[1];
    volumetric.fogColor[2] = g_fogColor[2];
    volumetric.useShadows = g_useShadows;
    volumetric.shadowBias = g_shadowBias;
    volumetric.shadowStrength = g_shadowStrength;

    // Camera orbit
    auto& camera = chain.get<CameraOperator>("camera");
    if (g_autoOrbit) {
        float camAngle = time * g_orbitSpeed;
        float camDist = 5.0f;
        camera.position(
            std::sin(camAngle) * camDist,
            2.5f + std::sin(time * 0.3f) * 0.3f,
            4.0f + std::cos(camAngle) * 2.0f
        );
    }
    camera.target(0.0f, 2.5f, -4.0f);
}

VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = false
}))
