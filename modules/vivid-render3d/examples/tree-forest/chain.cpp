// Tree Forest - Vivid Example
// L-System procedural trees with billboard leaf clusters
//
// Demonstrates the TreeMesh operator rendered through Render3D
// for unified shadow support and proper depth testing with scene geometry.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/gui/imgui.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Tree type selection
static int g_treeType = 0;  // 0=Deciduous, 1=Conifer, 2=Palm, 3=Willow, 4=Bushy, 5=Custom

// Tree geometry controls
static float g_trunkHeight = 2.0f;
static float g_trunkRadius = 0.12f;
static int g_lsystemIterations = 4;
static float g_branchAngle = 25.0f;
static float g_lengthScale = 0.9f;
static float g_radiusScale = 0.7f;

// Leaf controls
static int g_leafDensity = 8;
static float g_leafSize = 0.25f;
static float g_clusterRadius = 0.35f;

// Wind controls
static float g_windStrength = 0.2f;
static float g_windSpeed = 0.8f;
static float g_leafFlutter = 0.3f;

// Field controls
static int g_treeCount = 5;
static float g_fieldWidth = 20.0f;
static float g_fieldDepth = 20.0f;

// Color controls
static float g_trunkBaseColor[3] = {0.25f, 0.15f, 0.08f};
static float g_trunkTipColor[3] = {0.35f, 0.25f, 0.15f};
static float g_leafColor[3] = {0.15f, 0.4f, 0.1f};

// Leaf texture (optional - enable if you have leaf textures)
static bool g_useLeafTexture = false;

// Camera controls
static float g_orbitSpeed = 0.05f;
static float g_orbitRadius = 15.0f;
static float g_cameraHeight = 4.0f;
static bool g_autoOrbit = true;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // SCENE - Ground plane for the forest
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane - earthy brown
    auto& ground = scene.add<Box>("ground",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.05f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(30.0f, 0.1f, 30.0f)),
        glm::vec4(0.12f, 0.1f, 0.06f, 1.0f));

    // =========================================================================
    // CAMERA - Orbiting view of the forest
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(12.0f, 4.0f, 12.0f);
    camera.target(0.0f, 2.0f, 0.0f);
    camera.fov(50.0f);
    camera.nearPlane(0.1f);
    camera.farPlane(150.0f);

    // =========================================================================
    // LIGHTING - Warm sunlight with cool fill
    // =========================================================================

    // Main sun - warm afternoon light with shadows
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(-0.5f, -1.0f, -0.3f);
    sun.color(1.0f, 0.95f, 0.85f);
    sun.intensity = 1.2f;
    sun.castShadow(true);
    sun.shadowBias(0.003f);

    // Sky fill light - cool blue from above
    auto& sky = chain.add<DirectionalLight>("sky");
    sky.direction(0.0f, -1.0f, 0.0f);
    sky.color(0.6f, 0.7f, 0.9f);
    sky.intensity = 0.3f;

    // =========================================================================
    // TREE MESH - L-System procedural trees rendered through Render3D
    // =========================================================================

    auto& trees = chain.add<TreeMesh>("trees");

    // Start with Deciduous preset
    trees.setTreeType(TreeMesh::TreeType::Deciduous);

    // Field size
    trees.fieldWidth = g_fieldWidth;
    trees.fieldDepth = g_fieldDepth;
    trees.treeCount = g_treeCount;

    // Wind animation
    trees.windStrength = g_windStrength;
    trees.windSpeed = g_windSpeed;
    trees.leafFlutter = g_leafFlutter;

    // Colors
    trees.trunkBaseColor[0] = g_trunkBaseColor[0];
    trees.trunkBaseColor[1] = g_trunkBaseColor[1];
    trees.trunkBaseColor[2] = g_trunkBaseColor[2];
    trees.trunkTipColor[0] = g_trunkTipColor[0];
    trees.trunkTipColor[1] = g_trunkTipColor[1];
    trees.trunkTipColor[2] = g_trunkTipColor[2];
    trees.leafColor[0] = g_leafColor[0];
    trees.leafColor[1] = g_leafColor[1];
    trees.leafColor[2] = g_leafColor[2];

    // Enable shadow casting
    trees.castShadow = true;
    trees.receiveShadow = true;

    // =========================================================================
    // RENDER3D - Unified rendering with trees in shadow map
    // =========================================================================

    auto& render = chain.add<Render3D>("render3d");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sun);
    render.addLight(&sky);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.15f);
    render.setClearColor(0.5f, 0.7f, 0.95f);  // Sky blue background
    render.setResolution(1920, 1080);
    render.setShadows(true);
    render.setShadowMapResolution(2048);

    // Add procedural trees for unified rendering + shadows
    render.addProceduralMesh(&trees);

    // =========================================================================
    // POST PROCESSING - Subtle bloom for dreamy look
    // =========================================================================

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("render3d");
    bloom.threshold = 0.85f;
    bloom.intensity = 0.2f;
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

    auto& trees = chain.get<TreeMesh>("trees");

    // ImGui controls
    if (vivid::imgui::isVisible()) {
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 680), ImGuiCond_FirstUseEver);
        ImGui::Begin("Tree Forest");

        // Tree type selector
        ImGui::SeparatorText("Tree Type");
        const char* treeTypes[] = { "Deciduous", "Conifer", "Palm", "Willow", "Bushy", "Custom" };
        if (ImGui::Combo("Type", &g_treeType, treeTypes, 6)) {
            trees.setTreeType(static_cast<TreeMesh::TreeType>(g_treeType));
            // Update UI to match preset values
            g_trunkHeight = static_cast<float>(trees.trunkHeight);
            g_trunkRadius = static_cast<float>(trees.trunkRadius);
            g_lsystemIterations = static_cast<int>(trees.lsystemIterations);
            g_branchAngle = static_cast<float>(trees.branchAngle);
            g_lengthScale = static_cast<float>(trees.lengthScale);
            g_radiusScale = static_cast<float>(trees.radiusScale);
            g_leafDensity = static_cast<int>(trees.leafDensity);
            g_leafSize = static_cast<float>(trees.leafSize);
            g_clusterRadius = static_cast<float>(trees.clusterRadius);
            g_windStrength = static_cast<float>(trees.windStrength);
            g_windSpeed = static_cast<float>(trees.windSpeed);
            g_trunkBaseColor[0] = trees.trunkBaseColor[0];
            g_trunkBaseColor[1] = trees.trunkBaseColor[1];
            g_trunkBaseColor[2] = trees.trunkBaseColor[2];
            g_trunkTipColor[0] = trees.trunkTipColor[0];
            g_trunkTipColor[1] = trees.trunkTipColor[1];
            g_trunkTipColor[2] = trees.trunkTipColor[2];
            g_leafColor[0] = trees.leafColor[0];
            g_leafColor[1] = trees.leafColor[1];
            g_leafColor[2] = trees.leafColor[2];
        }

        ImGui::SeparatorText("Tree Structure");
        ImGui::SliderFloat("Trunk Height", &g_trunkHeight, 0.5f, 10.0f);
        ImGui::SliderFloat("Trunk Radius", &g_trunkRadius, 0.02f, 0.5f);
        ImGui::SliderInt("L-System Iter", &g_lsystemIterations, 1, 7);
        ImGui::SliderFloat("Branch Angle", &g_branchAngle, 10.0f, 60.0f);
        ImGui::SliderFloat("Length Scale", &g_lengthScale, 0.5f, 1.0f);
        ImGui::SliderFloat("Radius Scale", &g_radiusScale, 0.4f, 0.9f);

        ImGui::SeparatorText("Leaf Clusters");
        ImGui::SliderInt("Leaf Density", &g_leafDensity, 1, 30);
        ImGui::SliderFloat("Leaf Size", &g_leafSize, 0.05f, 1.0f);
        ImGui::SliderFloat("Cluster Radius", &g_clusterRadius, 0.1f, 1.5f);

        ImGui::SeparatorText("Wind Animation");
        ImGui::SliderFloat("Strength", &g_windStrength, 0.0f, 1.0f);
        ImGui::SliderFloat("Speed", &g_windSpeed, 0.1f, 3.0f);
        ImGui::SliderFloat("Leaf Flutter", &g_leafFlutter, 0.0f, 1.0f);

        ImGui::SeparatorText("Forest");
        ImGui::SliderInt("Tree Count", &g_treeCount, 1, 30);
        ImGui::SliderFloat("Field Width", &g_fieldWidth, 5.0f, 50.0f);
        ImGui::SliderFloat("Field Depth", &g_fieldDepth, 5.0f, 50.0f);

        ImGui::SeparatorText("Color & Texture");
        ImGui::ColorEdit3("Trunk Base", g_trunkBaseColor);
        ImGui::ColorEdit3("Trunk Tip", g_trunkTipColor);
        ImGui::ColorEdit3("Leaf Color", g_leafColor);
        if (ImGui::Checkbox("Use Leaf Texture", &g_useLeafTexture)) {
            if (g_useLeafTexture) {
                // Try to load leaf texture from assets folder
                trees.setLeafTexture("assets/textures/leaf_maple.png");
            } else {
                trees.clearLeafTexture();
            }
        }
        if (g_useLeafTexture && !trees.hasLeafTexture()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                "Texture not found. Place leaf_maple.png\nin assets/textures/");
        }

        ImGui::SeparatorText("Camera");
        ImGui::Checkbox("Auto Orbit", &g_autoOrbit);
        ImGui::SliderFloat("Orbit Speed", &g_orbitSpeed, 0.0f, 0.2f);
        ImGui::SliderFloat("Orbit Radius", &g_orbitRadius, 5.0f, 30.0f);
        ImGui::SliderFloat("Cam Height", &g_cameraHeight, 1.0f, 15.0f);

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
    camera.target(0.0f, g_trunkHeight * 0.5f, 0.0f);

    // Update tree parameters
    // Tree structure
    trees.trunkHeight = g_trunkHeight;
    trees.trunkRadius = g_trunkRadius;
    trees.lsystemIterations = g_lsystemIterations;
    trees.branchAngle = g_branchAngle;
    trees.lengthScale = g_lengthScale;
    trees.radiusScale = g_radiusScale;

    // Leaf clusters
    trees.leafDensity = g_leafDensity;
    trees.leafSize = g_leafSize;
    trees.clusterRadius = g_clusterRadius;

    // Wind
    trees.windStrength = g_windStrength;
    trees.windSpeed = g_windSpeed;
    trees.leafFlutter = g_leafFlutter;

    // Field
    trees.fieldWidth = g_fieldWidth;
    trees.fieldDepth = g_fieldDepth;
    trees.treeCount = g_treeCount;

    // Colors
    trees.trunkBaseColor[0] = g_trunkBaseColor[0];
    trees.trunkBaseColor[1] = g_trunkBaseColor[1];
    trees.trunkBaseColor[2] = g_trunkBaseColor[2];
    trees.trunkTipColor[0] = g_trunkTipColor[0];
    trees.trunkTipColor[1] = g_trunkTipColor[1];
    trees.trunkTipColor[2] = g_trunkTipColor[2];
    trees.leafColor[0] = g_leafColor[0];
    trees.leafColor[1] = g_leafColor[1];
    trees.leafColor[2] = g_leafColor[2];
}

VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = false
}))
