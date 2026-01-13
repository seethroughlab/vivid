// Grass Field - Vivid Example
// GPU-instanced grass with wind animation
//
// Demonstrates the GrassMesh operator rendered through Render3D
// for unified shadow support and proper depth testing with scene geometry.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/gui/imgui.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Wind controls
static float g_windStrength = 0.5f;
static float g_windSpeed = 1.0f;

// Field controls
static int g_bladeCount = 10000;
static float g_fieldWidth = 15.0f;
static float g_fieldDepth = 15.0f;

// Blade controls
static float g_bladeHeight = 0.4f;
static float g_bladeWidth = 0.03f;
static float g_heightVariation = 0.3f;

// Color controls
static float g_baseColor[3] = {0.15f, 0.35f, 0.08f};
static float g_tipColor[3] = {0.3f, 0.55f, 0.15f};

// Camera controls
static float g_orbitSpeed = 0.1f;
static float g_orbitRadius = 12.0f;
static float g_cameraHeight = 3.0f;
static bool g_autoOrbit = true;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // SCENE - Ground plane for grass field
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane - dark earth (mostly hidden by grass)
    auto& ground = scene.add<Box>("ground",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.02f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(20.0f, 0.04f, 20.0f)),
        glm::vec4(0.12f, 0.1f, 0.06f, 1.0f));

    // =========================================================================
    // CAMERA - Orbiting view of the grass field
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(10.0f, 3.0f, 10.0f);
    camera.target(0.0f, 0.5f, 0.0f);
    camera.fov(50.0f);
    camera.nearPlane(0.1f);
    camera.farPlane(100.0f);

    // =========================================================================
    // LIGHTING - Warm sunlight with cool fill
    // =========================================================================

    // Main sun - warm afternoon light with shadows
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(-0.5f, -1.0f, -0.3f);
    sun.color(1.0f, 0.95f, 0.85f);
    sun.intensity = 1.2f;
    sun.castShadow(true);
    sun.shadowBias(0.001f);

    // Sky fill light - cool blue from above
    auto& sky = chain.add<DirectionalLight>("sky");
    sky.direction(0.0f, -1.0f, 0.0f);
    sky.color(0.6f, 0.7f, 0.9f);
    sky.intensity = 0.3f;

    // =========================================================================
    // GRASS MESH - Thousands of animated blades rendered through Render3D
    // =========================================================================

    auto& grass = chain.add<GrassMesh>("grass");

    // Field size
    grass.fieldWidth = g_fieldWidth;
    grass.fieldDepth = g_fieldDepth;
    grass.bladeCount = g_bladeCount;

    // Blade appearance
    grass.bladeHeight = g_bladeHeight;
    grass.bladeWidth = g_bladeWidth;
    grass.heightVariation = g_heightVariation;

    // Wind animation
    grass.windStrength = g_windStrength;
    grass.windSpeed = g_windSpeed;

    // Colors
    grass.baseColor[0] = g_baseColor[0];
    grass.baseColor[1] = g_baseColor[1];
    grass.baseColor[2] = g_baseColor[2];
    grass.tipColor[0] = g_tipColor[0];
    grass.tipColor[1] = g_tipColor[1];
    grass.tipColor[2] = g_tipColor[2];

    // Enable shadow casting
    grass.castShadow = true;
    grass.receiveShadow = true;

    // =========================================================================
    // RENDER3D - Unified rendering with grass in shadow map
    // =========================================================================

    auto& render = chain.add<Render3D>("render3d");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sun);
    render.addLight(&sky);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.1f);
    render.setClearColor(0.5f, 0.7f, 0.95f);  // Sky blue background
    render.setResolution(1920, 1080);
    render.setShadows(true);
    render.setShadowMapResolution(2048);

    // Add procedural grass for unified rendering + shadows
    render.addProceduralMesh(&grass);

    // =========================================================================
    // POST PROCESSING - Subtle bloom for dreamy feel
    // =========================================================================

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("render3d");
    bloom.threshold = 0.8f;
    bloom.intensity = 0.3f;
    bloom.radius = 8.0f;

    chain.output("bloom");

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
        ImGui::SetNextWindowSize(ImVec2(280, 500), ImGuiCond_FirstUseEver);
        ImGui::Begin("Grass Field");

        ImGui::SeparatorText("Wind Animation");
        ImGui::SliderFloat("Strength", &g_windStrength, 0.0f, 2.0f);
        ImGui::SliderFloat("Speed", &g_windSpeed, 0.1f, 5.0f);

        ImGui::SeparatorText("Field");
        ImGui::SliderInt("Blade Count", &g_bladeCount, 1000, 50000);
        ImGui::SliderFloat("Width", &g_fieldWidth, 5.0f, 30.0f);
        ImGui::SliderFloat("Depth", &g_fieldDepth, 5.0f, 30.0f);

        ImGui::SeparatorText("Blade");
        ImGui::SliderFloat("Height", &g_bladeHeight, 0.1f, 1.0f);
        ImGui::SliderFloat("Width##blade", &g_bladeWidth, 0.01f, 0.1f);
        ImGui::SliderFloat("Variation", &g_heightVariation, 0.0f, 0.5f);

        ImGui::SeparatorText("Color");
        ImGui::ColorEdit3("Base", g_baseColor);
        ImGui::ColorEdit3("Tip", g_tipColor);

        ImGui::SeparatorText("Camera");
        ImGui::Checkbox("Auto Orbit", &g_autoOrbit);
        ImGui::SliderFloat("Orbit Speed", &g_orbitSpeed, 0.0f, 0.5f);
        ImGui::SliderFloat("Orbit Radius", &g_orbitRadius, 5.0f, 25.0f);
        ImGui::SliderFloat("Height##cam", &g_cameraHeight, 1.0f, 10.0f);

        ImGui::Separator();
        ImGui::Text("Press TAB to hide panel");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        ImGui::End();
    }

    // Camera orbit
    auto& camera = chain.get<CameraOperator>("camera");
    if (g_autoOrbit) {
        float camX = std::cos(time * g_orbitSpeed) * g_orbitRadius;
        float camZ = std::sin(time * g_orbitSpeed) * g_orbitRadius;
        camera.position(camX, g_cameraHeight, camZ);
    }
    camera.target(0.0f, g_bladeHeight * 0.5f, 0.0f);

    // Update grass parameters
    auto& grass = chain.get<GrassMesh>("grass");

    // Wind
    grass.windStrength = g_windStrength;
    grass.windSpeed = g_windSpeed;

    // Field (regenerates instances if changed)
    grass.fieldWidth = g_fieldWidth;
    grass.fieldDepth = g_fieldDepth;
    grass.bladeCount = g_bladeCount;

    // Blade
    grass.bladeHeight = g_bladeHeight;
    grass.bladeWidth = g_bladeWidth;
    grass.heightVariation = g_heightVariation;

    // Colors
    grass.baseColor[0] = g_baseColor[0];
    grass.baseColor[1] = g_baseColor[1];
    grass.baseColor[2] = g_baseColor[2];
    grass.tipColor[0] = g_tipColor[0];
    grass.tipColor[1] = g_tipColor[1];
    grass.tipColor[2] = g_tipColor[2];
}

VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = false
}))
