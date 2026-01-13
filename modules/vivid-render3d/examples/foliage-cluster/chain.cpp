// Foliage Cluster - Vivid Example
// GPU-instanced procedural fronds with wind animation
//
// Demonstrates the FoliageMesh operator rendered through Render3D
// for unified shadow support and proper depth testing with scene geometry.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/gui/imgui.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Plant type selection
static int g_plantType = 0;  // 0=Fern, 1=PalmFrond, 2=Grass, 3=Custom

// Frond geometry controls
static float g_stemLength = 1.0f;
static float g_stemCurve = 0.4f;
static int g_leafletPairs = 8;
static float g_leafletWidth = 0.15f;
static float g_leafletLength = 0.25f;
static float g_leafletAngle = 45.0f;
static float g_sizeVariation = 0.3f;

// Wind controls
static float g_windStrength = 0.3f;
static float g_windSpeed = 0.8f;

// Field controls
static int g_frondCount = 150;
static float g_fieldWidth = 10.0f;
static float g_fieldDepth = 10.0f;

// Color controls
static float g_baseColor[3] = {0.08f, 0.18f, 0.04f};
static float g_tipColor[3] = {0.15f, 0.35f, 0.08f};

// Camera controls
static float g_orbitSpeed = 0.08f;
static float g_orbitRadius = 8.0f;
static float g_cameraHeight = 2.0f;
static bool g_autoOrbit = true;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // SCENE - Ground plane for foliage field
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane - earthy brown
    auto& ground = scene.add<Box>("ground",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.05f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(15.0f, 0.1f, 15.0f)),
        glm::vec4(0.15f, 0.12f, 0.08f, 1.0f));

    // =========================================================================
    // CAMERA - Orbiting view of the foliage field
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(6.0f, 2.0f, 6.0f);
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
    sun.shadowBias(0.002f);

    // Sky fill light - cool blue from above
    auto& sky = chain.add<DirectionalLight>("sky");
    sky.direction(0.0f, -1.0f, 0.0f);
    sky.color(0.6f, 0.7f, 0.9f);
    sky.intensity = 0.3f;

    // =========================================================================
    // FOLIAGE MESH - Procedural ferns rendered through Render3D
    // =========================================================================

    auto& foliage = chain.add<FoliageMesh>("foliage");

    // Start with Fern preset
    foliage.setPlantType(FoliageMesh::PlantType::Fern);

    // Field size
    foliage.fieldWidth = g_fieldWidth;
    foliage.fieldDepth = g_fieldDepth;
    foliage.frondCount = g_frondCount;

    // Wind animation
    foliage.windStrength = g_windStrength;
    foliage.windSpeed = g_windSpeed;

    // Colors
    foliage.baseColor[0] = g_baseColor[0];
    foliage.baseColor[1] = g_baseColor[1];
    foliage.baseColor[2] = g_baseColor[2];
    foliage.tipColor[0] = g_tipColor[0];
    foliage.tipColor[1] = g_tipColor[1];
    foliage.tipColor[2] = g_tipColor[2];

    // Enable shadow casting
    foliage.castShadow = true;
    foliage.receiveShadow = true;

    // =========================================================================
    // RENDER3D - Unified rendering with foliage in shadow map
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

    // Add procedural foliage for unified rendering + shadows
    render.addProceduralMesh(&foliage);

    // =========================================================================
    // POST PROCESSING - Subtle bloom
    // =========================================================================

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("render3d");
    bloom.threshold = 0.85f;
    bloom.intensity = 0.25f;
    bloom.radius = 6.0f;

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

    auto& foliage = chain.get<FoliageMesh>("foliage");

    // ImGui controls
    if (vivid::imgui::isVisible()) {
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 620), ImGuiCond_FirstUseEver);
        ImGui::Begin("Foliage Cluster");

        // Plant type selector
        ImGui::SeparatorText("Plant Type");
        const char* plantTypes[] = { "Fern", "Palm Frond", "Grass", "Custom" };
        if (ImGui::Combo("Type", &g_plantType, plantTypes, 4)) {
            foliage.setPlantType(static_cast<FoliageMesh::PlantType>(g_plantType));
            // Update UI to match preset values
            g_stemLength = static_cast<float>(foliage.stemLength);
            g_stemCurve = static_cast<float>(foliage.stemCurve);
            g_leafletPairs = static_cast<int>(foliage.leafletPairs);
            g_leafletWidth = static_cast<float>(foliage.leafletWidth);
            g_leafletLength = static_cast<float>(foliage.leafletLength);
            g_leafletAngle = static_cast<float>(foliage.leafletAngle);
            g_sizeVariation = static_cast<float>(foliage.sizeVariation);
            g_windStrength = static_cast<float>(foliage.windStrength);
            g_windSpeed = static_cast<float>(foliage.windSpeed);
            g_baseColor[0] = foliage.baseColor[0];
            g_baseColor[1] = foliage.baseColor[1];
            g_baseColor[2] = foliage.baseColor[2];
            g_tipColor[0] = foliage.tipColor[0];
            g_tipColor[1] = foliage.tipColor[1];
            g_tipColor[2] = foliage.tipColor[2];
        }

        ImGui::SeparatorText("Frond Geometry");
        ImGui::SliderFloat("Stem Length", &g_stemLength, 0.2f, 3.0f);
        ImGui::SliderFloat("Stem Curve", &g_stemCurve, 0.0f, 1.0f);
        ImGui::SliderInt("Leaflet Pairs", &g_leafletPairs, 2, 20);
        ImGui::SliderFloat("Leaflet Width", &g_leafletWidth, 0.02f, 0.5f);
        ImGui::SliderFloat("Leaflet Length", &g_leafletLength, 0.05f, 0.8f);
        ImGui::SliderFloat("Leaflet Angle", &g_leafletAngle, 10.0f, 80.0f);
        ImGui::SliderFloat("Size Variation", &g_sizeVariation, 0.0f, 0.5f);

        ImGui::SeparatorText("Wind Animation");
        ImGui::SliderFloat("Strength", &g_windStrength, 0.0f, 1.5f);
        ImGui::SliderFloat("Speed", &g_windSpeed, 0.1f, 3.0f);

        ImGui::SeparatorText("Field");
        ImGui::SliderInt("Frond Count", &g_frondCount, 10, 500);
        ImGui::SliderFloat("Width", &g_fieldWidth, 5.0f, 25.0f);
        ImGui::SliderFloat("Depth", &g_fieldDepth, 5.0f, 25.0f);

        ImGui::SeparatorText("Color");
        ImGui::ColorEdit3("Base", g_baseColor);
        ImGui::ColorEdit3("Tip", g_tipColor);

        ImGui::SeparatorText("Camera");
        ImGui::Checkbox("Auto Orbit", &g_autoOrbit);
        ImGui::SliderFloat("Orbit Speed", &g_orbitSpeed, 0.0f, 0.3f);
        ImGui::SliderFloat("Orbit Radius", &g_orbitRadius, 3.0f, 15.0f);
        ImGui::SliderFloat("Cam Height", &g_cameraHeight, 0.5f, 6.0f);

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
    camera.target(0.0f, g_stemLength * 0.4f, 0.0f);

    // Update foliage parameters
    // Frond geometry
    foliage.stemLength = g_stemLength;
    foliage.stemCurve = g_stemCurve;
    foliage.leafletPairs = g_leafletPairs;
    foliage.leafletWidth = g_leafletWidth;
    foliage.leafletLength = g_leafletLength;
    foliage.leafletAngle = g_leafletAngle;
    foliage.sizeVariation = g_sizeVariation;

    // Wind
    foliage.windStrength = g_windStrength;
    foliage.windSpeed = g_windSpeed;

    // Field
    foliage.fieldWidth = g_fieldWidth;
    foliage.fieldDepth = g_fieldDepth;
    foliage.frondCount = g_frondCount;

    // Colors
    foliage.baseColor[0] = g_baseColor[0];
    foliage.baseColor[1] = g_baseColor[1];
    foliage.baseColor[2] = g_baseColor[2];
    foliage.tipColor[0] = g_tipColor[0];
    foliage.tipColor[1] = g_tipColor[1];
    foliage.tipColor[2] = g_tipColor[2];
}

VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = false
}))
