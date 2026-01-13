// Streetlight Fog - Vivid Example
// Volumetric lighting through fog, inspired by ISLANDS: Non-Places
//
// A solitary streetlight in thick fog, with god rays streaming through
// the atmospheric haze. Evokes the liminal, melancholic mood of non-places.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/gui/imgui.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Volumetric lighting controls
static float g_density = 0.05f;
static float g_intensity = 1.5f;
static float g_maxDistance = 20.0f;
static float g_anisotropy = 0.2f;
static int g_raySteps = 48;
static float g_fogColor[3] = {0.01f, 0.015f, 0.025f};

// Light controls
static float g_lightIntensity = 3.0f;
static float g_spotAngle = 45.0f;
static float g_spotBlend = 0.3f;
static float g_lightColor[3] = {1.0f, 0.9f, 0.7f};

// Camera controls
static float g_orbitSpeed = 0.15f;
static float g_orbitRadius = 7.0f;
static float g_cameraHeight = 2.0f;
static bool g_autoOrbit = true;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // SCENE - Ground and streetlight geometry
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane - dark asphalt
    auto& ground = scene.add<Box>("ground",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.05f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(20.0f, 0.1f, 20.0f)),
        glm::vec4(0.08f, 0.08f, 0.08f, 1.0f));  // Dark gray asphalt

    // Streetlight pole
    auto& pole = scene.add<Cylinder>("pole",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f)),
        glm::vec4(0.15f, 0.15f, 0.15f, 1.0f));  // Dark metal
    pole.radius(0.05f);
    pole.height(4.0f);
    pole.segments(12);

    // Lamp arm (horizontal piece)
    auto& arm = scene.add<Cylinder>("arm",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.4f, 4.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1)),
        glm::vec4(0.15f, 0.15f, 0.15f, 1.0f));
    arm.radius(0.03f);
    arm.height(0.8f);
    arm.segments(8);

    // Lamp housing (slightly emissive)
    auto& lamp = scene.add<Box>("lamp",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.8f, 3.9f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.15f, 0.1f, 0.15f)),
        glm::vec4(0.2f, 0.18f, 0.15f, 1.0f));

    // =========================================================================
    // CAMERA - Low angle looking up at the streetlight
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(5.0f, 1.5f, 5.0f);
    camera.target(0.0f, 3.0f, 0.0f);
    camera.fov(50.0f);
    camera.nearPlane(0.1f);
    camera.farPlane(100.0f);

    // =========================================================================
    // LIGHTING - Warm streetlight with ambient darkness
    // =========================================================================

    // Main streetlight - spot light pointing down to create visible cone
    auto& streetlight = chain.add<SpotLight>("streetlight");
    streetlight.position(0.8f, 3.85f, 0.0f);
    streetlight.direction(0.0f, -1.0f, 0.0f);  // Point straight down
    streetlight.color(g_lightColor[0], g_lightColor[1], g_lightColor[2]);
    streetlight.intensity = g_lightIntensity;
    streetlight.range = 15.0f;
    streetlight.spotAngle = g_spotAngle;
    streetlight.spotBlend = g_spotBlend;

    // Very dim ambient (moon/sky)
    auto& ambient = chain.add<DirectionalLight>("ambient");
    ambient.direction(0, -1, 0.5f);
    ambient.color(0.1f, 0.12f, 0.15f);  // Cool blue-gray
    ambient.intensity = 0.2f;

    // =========================================================================
    // RENDER3D - Base scene with depth output for post-processing
    // =========================================================================

    auto& render = chain.add<Render3D>("render3d");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&streetlight);
    render.addLight(&ambient);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.05f);  // Very dark ambient
    render.setClearColor(0.02f, 0.02f, 0.03f);  // Near-black night sky
    render.setResolution(1920, 1080);
    render.setDepthOutput(true);  // Required for volumetric lighting!

    // =========================================================================
    // VOLUMETRIC LIGHTING - God rays through the fog
    // =========================================================================

    auto& volumetric = chain.add<VolumetricLighting>("volumetric");
    volumetric.input(&render);
    volumetric.lightInput(&streetlight);
    volumetric.cameraInput(&camera);

    // Fog/atmosphere parameters - visible light cone
    volumetric.density = g_density;
    volumetric.intensity = g_intensity;
    volumetric.maxDistance = g_maxDistance;
    volumetric.anisotropy = g_anisotropy;
    volumetric.raySteps = g_raySteps;
    volumetric.fogColor[0] = g_fogColor[0];
    volumetric.fogColor[1] = g_fogColor[1];
    volumetric.fogColor[2] = g_fogColor[2];

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
        ImGui::SetNextWindowSize(ImVec2(320, 480), ImGuiCond_FirstUseEver);
        ImGui::Begin("Volumetric Lighting");

        ImGui::SeparatorText("Volumetric Effect");
        ImGui::SliderFloat("Density", &g_density, 0.0f, 0.3f);
        ImGui::SliderFloat("Intensity", &g_intensity, 0.0f, 5.0f);
        ImGui::SliderFloat("Max Distance", &g_maxDistance, 5.0f, 50.0f);
        ImGui::SliderFloat("Anisotropy", &g_anisotropy, -1.0f, 1.0f);
        ImGui::SliderInt("Ray Steps", &g_raySteps, 8, 128);
        ImGui::ColorEdit3("Fog Color", g_fogColor);

        ImGui::SeparatorText("Spotlight");
        ImGui::SliderFloat("Light Intensity", &g_lightIntensity, 0.0f, 10.0f);
        ImGui::SliderFloat("Spot Angle", &g_spotAngle, 10.0f, 90.0f);
        ImGui::SliderFloat("Spot Blend", &g_spotBlend, 0.0f, 1.0f);
        ImGui::ColorEdit3("Light Color", g_lightColor);

        ImGui::SeparatorText("Camera");
        ImGui::Checkbox("Auto Orbit", &g_autoOrbit);
        ImGui::SliderFloat("Orbit Speed", &g_orbitSpeed, 0.0f, 1.0f);
        ImGui::SliderFloat("Orbit Radius", &g_orbitRadius, 3.0f, 15.0f);
        ImGui::SliderFloat("Camera Height", &g_cameraHeight, 0.5f, 5.0f);

        ImGui::Separator();
        ImGui::Text("Press TAB to hide panel");

        ImGui::End();
    }

    // Orbit camera around the streetlight
    auto& camera = chain.get<CameraOperator>("camera");
    static float manualAzimuth = 0.0f;
    if (g_autoOrbit) {
        float camX = std::cos(time * g_orbitSpeed) * g_orbitRadius;
        float camZ = std::sin(time * g_orbitSpeed) * g_orbitRadius;
        camera.position(camX, g_cameraHeight, camZ);
    } else {
        float camX = std::cos(manualAzimuth) * g_orbitRadius;
        float camZ = std::sin(manualAzimuth) * g_orbitRadius;
        camera.position(camX, g_cameraHeight, camZ);
    }
    camera.target(0.0f, 2.0f, 0.0f);

    // Apply spotlight settings
    auto& streetlight = chain.get<SpotLight>("streetlight");
    float flicker = 1.0f + std::sin(time * 8.0f) * 0.02f + std::sin(time * 13.0f) * 0.01f;
    streetlight.intensity = g_lightIntensity * flicker;
    streetlight.spotAngle = g_spotAngle;
    streetlight.spotBlend = g_spotBlend;
    streetlight.color(g_lightColor[0], g_lightColor[1], g_lightColor[2]);

    // Apply volumetric settings
    auto& volumetric = chain.get<VolumetricLighting>("volumetric");
    volumetric.density = g_density;
    volumetric.intensity = g_intensity;
    volumetric.maxDistance = g_maxDistance;
    volumetric.anisotropy = g_anisotropy;
    volumetric.raySteps = g_raySteps;
    volumetric.fogColor[0] = g_fogColor[0];
    volumetric.fogColor[1] = g_fogColor[1];
    volumetric.fogColor[2] = g_fogColor[2];
}

VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = false
}))
