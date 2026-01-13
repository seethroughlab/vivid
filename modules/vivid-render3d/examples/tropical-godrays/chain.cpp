// Tropical God Rays - Vivid Example
// Radial blur god rays with procedural palm fronds
// Inspired by ISLANDS: Non-Places aesthetic
//
// Demonstrates:
// - Procedural palm fronds using FoliageMesh + Render3D
// - Radial blur god rays effect (bright beams from light source)
// - Animated swaying fronds with GPU wind
// - Monochromatic teal/cyan color palette

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/gui/imgui.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Constants
static const float TRUNK_HEIGHT = 4.0f;
static const float PI = 3.14159265f;

// Frond controls
static int g_frondCount = 35;
static float g_stemLength = 3.0f;
static float g_stemCurve = 0.5f;
static int g_leafletPairs = 12;
static float g_leafletWidth = 0.15f;
static float g_leafletLength = 0.6f;
static float g_leafletAngle = 35.0f;

// Wind controls
static float g_windStrength = 0.4f;   // Visible swaying
static float g_windSpeed = 0.8f;

// God rays controls - tuned for visible effect
static float g_rayExposure = 0.25f;
static float g_rayDecay = 0.99f;     // Slight decay for falloff
static float g_rayDensity = 1.2f;
static float g_rayWeight = 0.5f;
static int g_raySamples = 120;
static float g_rayThreshold = 0.8f;  // Catch the bright orb
static float g_rayBlend = 1.0f;

// Light controls
static float g_lightIntensity = 3.0f;
static float g_spotAngle = 55.0f;
static float g_spotBlend = 0.3f;

// Camera controls
static bool g_autoOrbit = false;
static float g_orbitSpeed = 0.1f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // SCENE - Ground and palm trunk
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane - dark teal
    auto& ground = scene.add<Box>("ground",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.05f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(25.0f, 0.1f, 25.0f)),
        glm::vec4(0.02f, 0.03f, 0.04f, 1.0f));

    // Palm trunk - dark silhouette
    auto& trunk = scene.add<Cylinder>("trunk",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, TRUNK_HEIGHT / 2.0f, 0.0f)),
        glm::vec4(0.015f, 0.02f, 0.025f, 1.0f));
    trunk.radius(0.12f);
    trunk.height(TRUNK_HEIGHT);
    trunk.segments(8);

    // Emissive orb - bright cyan glow (ISLANDS aesthetic)
    // This is the bright source for the god rays
    auto& orb = scene.add<Sphere>("orb",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, 0.0f)),
        glm::vec4(3.0f, 4.0f, 5.0f, 1.0f));  // HDR bright cyan
    orb.radius(2.5f);
    orb.segments(20);

    // =========================================================================
    // CAMERA - Low angle looking up through fronds toward light
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(5.0f, 1.5f, 5.0f);
    camera.target(0.0f, 6.0f, 0.0f);
    camera.fov(65.0f);
    camera.nearPlane(0.1f);
    camera.farPlane(100.0f);

    // =========================================================================
    // LIGHTING - Spotlight from above
    // =========================================================================

    // Main light - spotlight pointing down through fronds
    auto& light = chain.add<SpotLight>("light");
    light.position(0.0f, 10.0f, 0.0f);
    light.direction(0.0f, -1.0f, 0.0f);
    light.color(0.6f, 0.9f, 1.0f);
    light.intensity = g_lightIntensity;
    light.range = 25.0f;
    light.spotAngle = g_spotAngle;
    light.spotBlend = g_spotBlend;
    light.castShadow(true);
    light.shadowBias(0.002f);

    // Minimal ambient - just enough to see silhouettes
    auto& ambient = chain.add<DirectionalLight>("ambient");
    ambient.direction(0, -1, 0);
    ambient.color(0.02f, 0.03f, 0.04f);
    ambient.intensity = 0.1f;

    // =========================================================================
    // PALM FRONDS - Procedural mesh with wind animation
    // =========================================================================

    auto& fronds = chain.add<FoliageMesh>("fronds");
    fronds.setPlantType(FoliageMesh::PlantType::PalmFrond);

    // Position fronds at top of trunk
    fronds.fieldWidth = 0.8f;
    fronds.fieldDepth = 0.8f;
    fronds.frondCount = g_frondCount;
    fronds.baseHeight = TRUNK_HEIGHT;

    // Frond geometry
    fronds.stemLength = g_stemLength;
    fronds.stemCurve = g_stemCurve;
    fronds.leafletPairs = g_leafletPairs;
    fronds.leafletWidth = g_leafletWidth;
    fronds.leafletLength = g_leafletLength;
    fronds.leafletAngle = g_leafletAngle;
    fronds.sizeVariation = 0.2f;

    // Wind animation
    fronds.windStrength = g_windStrength;
    fronds.windSpeed = g_windSpeed;

    // Dark silhouette colors to match ISLANDS aesthetic
    fronds.baseColor[0] = 0.01f;
    fronds.baseColor[1] = 0.02f;
    fronds.baseColor[2] = 0.025f;
    fronds.tipColor[0] = 0.02f;
    fronds.tipColor[1] = 0.035f;
    fronds.tipColor[2] = 0.04f;

    fronds.castShadow = true;
    fronds.receiveShadow = true;

    // =========================================================================
    // RENDER3D - Unified rendering with fronds
    // =========================================================================

    auto& render = chain.add<Render3D>("render3d");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&light);
    render.addLight(&ambient);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.8f);  // High ambient so orb's bright color shows through
    render.setClearColor(0.01f, 0.015f, 0.02f);  // Dark background
    render.setResolution(1920, 1080);
    render.setDepthOutput(true);
    render.setShadows(true);
    render.setShadowMapResolution(2048);

    render.addProceduralMesh(&fronds);

    // =========================================================================
    // GOD RAYS - Radial blur effect for bright light beams
    // =========================================================================

    auto& godrays = chain.add<GodRays>("godrays");
    godrays.setInput(&render);
    godrays.setCameraInput(&camera);
    godrays.setLightInput(&light);

    // God ray parameters
    godrays.exposure = g_rayExposure;
    godrays.decay = g_rayDecay;
    godrays.density = g_rayDensity;
    godrays.weight = g_rayWeight;
    godrays.samples = g_raySamples;
    godrays.threshold = g_rayThreshold;
    godrays.blend = g_rayBlend;

    chain.output("godrays");

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
        ImGui::SetNextWindowSize(ImVec2(340, 620), ImGuiCond_FirstUseEver);
        ImGui::Begin("Tropical God Rays");

        ImGui::SeparatorText("Palm Fronds");
        ImGui::SliderInt("Frond Count", &g_frondCount, 5, 50);
        ImGui::SliderFloat("Stem Length", &g_stemLength, 1.0f, 4.0f);
        ImGui::SliderFloat("Stem Curve", &g_stemCurve, 0.2f, 0.9f);
        ImGui::SliderInt("Leaflet Pairs", &g_leafletPairs, 6, 20);
        ImGui::SliderFloat("Leaflet Width", &g_leafletWidth, 0.02f, 0.3f);
        ImGui::SliderFloat("Leaflet Length", &g_leafletLength, 0.2f, 0.8f);
        ImGui::SliderFloat("Leaflet Angle", &g_leafletAngle, 15.0f, 60.0f);

        ImGui::SeparatorText("Wind Animation");
        ImGui::SliderFloat("Wind Strength", &g_windStrength, 0.0f, 0.5f);
        ImGui::SliderFloat("Wind Speed", &g_windSpeed, 0.1f, 1.5f);

        ImGui::SeparatorText("God Rays");
        ImGui::SliderFloat("Exposure", &g_rayExposure, 0.0f, 1.0f);
        ImGui::SliderFloat("Decay", &g_rayDecay, 0.9f, 1.0f);
        ImGui::SliderFloat("Density", &g_rayDensity, 0.5f, 2.0f);
        ImGui::SliderFloat("Weight", &g_rayWeight, 0.0f, 1.0f);
        ImGui::SliderInt("Samples", &g_raySamples, 32, 128);
        ImGui::SliderFloat("Threshold", &g_rayThreshold, 0.0f, 1.0f);
        ImGui::SliderFloat("Blend", &g_rayBlend, 0.0f, 1.0f);

        ImGui::SeparatorText("Spotlight");
        ImGui::SliderFloat("Light Intensity", &g_lightIntensity, 0.0f, 5.0f);
        ImGui::SliderFloat("Spot Angle", &g_spotAngle, 30.0f, 90.0f);
        ImGui::SliderFloat("Spot Blend", &g_spotBlend, 0.0f, 1.0f);

        ImGui::SeparatorText("Camera");
        ImGui::Checkbox("Auto Orbit", &g_autoOrbit);
        ImGui::SliderFloat("Orbit Speed", &g_orbitSpeed, 0.0f, 0.3f);

        ImGui::Separator();
        ImGui::Text("Press TAB to hide panel");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        ImGui::End();
    }

    // Update frond parameters (only when changed to avoid constant regeneration)
    auto& fronds = chain.get<FoliageMesh>("fronds");
    float frondParams[4];

    // Track previous values to detect changes
    static int prevFrondCount = g_frondCount;
    static float prevStemLength = g_stemLength;
    static float prevStemCurve = g_stemCurve;
    static int prevLeafletPairs = g_leafletPairs;
    static float prevLeafletWidth = g_leafletWidth;
    static float prevLeafletLength = g_leafletLength;
    static float prevLeafletAngle = g_leafletAngle;
    static float prevWindStrength = g_windStrength;
    static float prevWindSpeed = g_windSpeed;

    if (g_frondCount != prevFrondCount) {
        frondParams[0] = static_cast<float>(g_frondCount);
        fronds.setParam("frondCount", frondParams);
        prevFrondCount = g_frondCount;
    }
    if (g_stemLength != prevStemLength) {
        frondParams[0] = g_stemLength;
        fronds.setParam("stemLength", frondParams);
        prevStemLength = g_stemLength;
    }
    if (g_stemCurve != prevStemCurve) {
        frondParams[0] = g_stemCurve;
        fronds.setParam("stemCurve", frondParams);
        prevStemCurve = g_stemCurve;
    }
    if (g_leafletPairs != prevLeafletPairs) {
        frondParams[0] = static_cast<float>(g_leafletPairs);
        fronds.setParam("leafletPairs", frondParams);
        prevLeafletPairs = g_leafletPairs;
    }
    if (g_leafletWidth != prevLeafletWidth) {
        frondParams[0] = g_leafletWidth;
        fronds.setParam("leafletWidth", frondParams);
        prevLeafletWidth = g_leafletWidth;
    }
    if (g_leafletLength != prevLeafletLength) {
        frondParams[0] = g_leafletLength;
        fronds.setParam("leafletLength", frondParams);
        prevLeafletLength = g_leafletLength;
    }
    if (g_leafletAngle != prevLeafletAngle) {
        frondParams[0] = g_leafletAngle;
        fronds.setParam("leafletAngle", frondParams);
        prevLeafletAngle = g_leafletAngle;
    }
    if (g_windStrength != prevWindStrength) {
        frondParams[0] = g_windStrength;
        fronds.setParam("windStrength", frondParams);
        prevWindStrength = g_windStrength;
    }
    if (g_windSpeed != prevWindSpeed) {
        frondParams[0] = g_windSpeed;
        fronds.setParam("windSpeed", frondParams);
        prevWindSpeed = g_windSpeed;
    }

    // Update spotlight
    auto& light = chain.get<SpotLight>("light");
    light.intensity = g_lightIntensity;
    light.spotAngle = g_spotAngle;
    light.spotBlend = g_spotBlend;

    // Update god rays parameters
    auto& godrays = chain.get<GodRays>("godrays");
    godrays.exposure = g_rayExposure;
    godrays.decay = g_rayDecay;
    godrays.density = g_rayDensity;
    godrays.weight = g_rayWeight;
    godrays.samples = g_raySamples;
    godrays.threshold = g_rayThreshold;
    godrays.blend = g_rayBlend;

    // Subtle orb pulsing
    auto& scene = chain.get<SceneComposer>("scene");
    auto& entries = scene.entries();
    float pulse = 0.9f + 0.1f * std::sin(time * 1.5f);
    entries[2].color = glm::vec4(0.8f * pulse, 0.95f * pulse, 1.0f * pulse, 1.0f);

    // Camera orbit
    auto& camera = chain.get<CameraOperator>("camera");
    if (g_autoOrbit) {
        float camAngle = time * g_orbitSpeed;
        float camDist = 8.0f;
        camera.position(
            std::cos(camAngle) * camDist,
            2.0f + std::sin(time * 0.2f) * 0.5f,
            std::sin(camAngle) * camDist
        );
    }
    camera.target(0.0f, 5.0f, 0.0f);
}

VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = false
}))
