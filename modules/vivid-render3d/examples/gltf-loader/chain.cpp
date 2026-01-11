// GLTF Demo - Load and display 3D models from GLTF/GLB files
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/asset_loader.h>
#include <vivid/gui/imgui.h>
#include <filesystem>
#include <algorithm>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;
namespace fs = std::filesystem;

// Model list (populated at startup)
static std::vector<std::string> g_models;
static int g_currentModel = 0;
static float g_cameraDistance = 3.0f;

// Lighting controls
static float g_emissiveStrength = 1.0f;
static float g_lightIntensity = 2.0f;
static float g_lightDir[3] = {1.0f, 2.0f, 1.0f};
static bool g_autoRotate = true;

// Bloom controls
static float g_bloomThreshold = 0.6f;
static float g_bloomIntensity = 1.0f;
static float g_bloomRadius = 15.0f;

// Environment
static bool g_showSkybox = true;

// Find all .glb files in a directory (relative to project)
std::vector<std::string> findModels(const std::string& relativeDir) {
    std::vector<std::string> models;

    // Get absolute path using AssetLoader's project directory
    fs::path projectDir = AssetLoader::instance().projectDir();
    fs::path directory = projectDir.empty() ? fs::path(relativeDir) : projectDir / relativeDir;

    try {
        if (!fs::exists(directory)) {
            std::cerr << "[gltf-demo] Directory not found: " << directory << std::endl;
            return models;
        }
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.path().extension() == ".glb" || entry.path().extension() == ".gltf") {
                models.push_back(entry.path().string());
            }
        }
        std::sort(models.begin(), models.end());
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[gltf-demo] Error scanning directory: " << e.what() << std::endl;
    }
    return models;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Find all models
    g_models = findModels("assets/models");
    if (g_models.empty()) {
        std::cerr << "[gltf-demo] No models found in assets/models/" << std::endl;
        return;
    }

    // Load first model
    auto& model = chain.add<GLTFLoader>("model");
    model.file(g_models[g_currentModel]);
    model.loadTextures(true);
    model.computeTangents(true);
    model.scale = 1.0f;

    // Create scene
    auto& scene = SceneComposer::create(chain, "scene");
    scene.add(&model, glm::mat4(1.0f), glm::vec4(1.0f));

    // Camera - will be adjusted based on model bounds
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0, 0);
    camera.distance(3.0f);
    camera.elevation(0.2f);
    camera.fov(50.0f);

    // Lighting
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(1, 2, 1);
    sun.color(1.0f, 0.98f, 0.95f);  // Warm white
    sun.intensity = 2.0f;

    // IBL environment (now a proper chain operator)
    auto& ibl = chain.add<IBLEnvironment>("ibl");
    ibl.setHdrFile("assets/hdris/warm_reception_dinner_4k.hdr");

    // Render with PBR + IBL
    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sun);
    render.setShadingMode(ShadingMode::PBR);
    render.setIbl(true);
    render.setEnvironmentInput(&ibl);
    render.setShowSkybox(true);
    render.setMetallic(0.0f);
    render.setRoughness(0.5f);
    render.setClearColor(0.1f, 0.1f, 0.15f);

    // Bloom post-process for emissive glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("render");
    bloom.threshold = g_bloomThreshold;
    bloom.intensity = g_bloomIntensity;
    bloom.radius = g_bloomRadius;
    bloom.passes = 2;

    chain.output("bloom");

    // Initialize ImGui
    vivid::imgui::init(ctx);

    std::cout << "[gltf-demo] Found " << g_models.size() << " models" << std::endl;
    std::cout << "[gltf-demo] SPACE: cycle models, Scroll: zoom, TAB: UI, V: vsync" << std::endl;
}

void fitCameraToModel(CameraOperator& camera, const Bounds3D& bounds) {
    // Calculate camera distance to fit model in view
    float radius = bounds.radius();
    float fovRad = glm::radians(50.0f);  // Match camera FOV
    float distance = radius / std::sin(fovRad * 0.5f);

    // Add some padding
    distance *= 1.5f;

    // Clamp to reasonable range
    distance = std::max(0.5f, std::min(distance, 100.0f));

    camera.orbitCenter(bounds.center());
    camera.distance(distance);
    g_cameraDistance = distance;  // Track for scroll zoom
}

void update(Context& ctx) {
    // Skip if no models were found
    if (g_models.empty()) return;

    auto& camera = ctx.chain().get<CameraOperator>("camera");
    auto& model = ctx.chain().get<GLTFLoader>("model");
    auto& sun = ctx.chain().get<DirectionalLight>("sun");

    // Fit camera on first frame after model loads
    static bool needsFit = true;
    if (model.isLoaded() && needsFit) {
        fitCameraToModel(camera, model.bounds());
        needsFit = false;
    }

    // TAB toggles ImGui
    if (ctx.key(GLFW_KEY_TAB).pressed) {
        vivid::imgui::toggleVisible();
    }

    // ImGui controls
    if (vivid::imgui::isVisible()) {
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 420), ImGuiCond_FirstUseEver);
        ImGui::Begin("Lighting Controls");

        ImGui::SeparatorText("Emission");
        ImGui::SliderFloat("Emissive Strength", &g_emissiveStrength, 0.0f, 10.0f);

        ImGui::SeparatorText("Bloom (Glow)");
        ImGui::SliderFloat("Threshold", &g_bloomThreshold, 0.0f, 1.0f);
        ImGui::SliderFloat("Bloom Intensity", &g_bloomIntensity, 0.0f, 3.0f);
        ImGui::SliderFloat("Radius", &g_bloomRadius, 1.0f, 30.0f);

        ImGui::SeparatorText("Environment");
        ImGui::Checkbox("Show Skybox", &g_showSkybox);

        ImGui::SeparatorText("Sun Light");
        ImGui::SliderFloat("Intensity", &g_lightIntensity, 0.0f, 10.0f);
        ImGui::SliderFloat3("Direction", g_lightDir, -1.0f, 1.0f);

        ImGui::SeparatorText("Camera");
        ImGui::Checkbox("Auto Rotate", &g_autoRotate);
        ImGui::SliderFloat("Distance", &g_cameraDistance, 0.5f, 20.0f);

        ImGui::SeparatorText("Model");
        std::string filename = fs::path(g_models[g_currentModel]).filename().string();
        ImGui::Text("Current: %s", filename.c_str());
        if (ImGui::Button("Next Model") && g_models.size() > 1) {
            g_currentModel = (g_currentModel + 1) % g_models.size();
            model.file(g_models[g_currentModel]);
            needsFit = true;
        }

        ImGui::End();
    }

    // Apply lighting settings every frame
    if (auto* mat = model.material()) {
        mat->emissiveStrength(g_emissiveStrength);
    }
    sun.intensity = g_lightIntensity;
    sun.direction(g_lightDir[0], g_lightDir[1], g_lightDir[2]);

    // Apply bloom settings
    auto& bloom = ctx.chain().get<Bloom>("bloom");
    bloom.threshold = g_bloomThreshold;
    bloom.intensity = g_bloomIntensity;
    bloom.radius = g_bloomRadius;

    // Apply environment settings
    auto& render = ctx.chain().get<Render3D>("render");
    render.setShowSkybox(g_showSkybox);

    // Scroll wheel zoom
    float scrollY = ctx.scroll().y;
    if (scrollY != 0.0f) {
        g_cameraDistance *= (1.0f - scrollY * 0.1f);  // Zoom in/out
        g_cameraDistance = std::max(0.5f, std::min(g_cameraDistance, 100.0f));
    }
    camera.distance(g_cameraDistance);

    // Camera orbit (auto or manual)
    static float manualAzimuth = 0.0f;
    if (g_autoRotate) {
        camera.azimuth(ctx.time() * 0.3f);
    } else {
        camera.azimuth(manualAzimuth);
    }

    // Spacebar cycles through models
    if (ctx.key(GLFW_KEY_SPACE).pressed && !g_models.empty()) {
        g_currentModel = (g_currentModel + 1) % g_models.size();
        model.file(g_models[g_currentModel]);
        needsFit = true;  // Fit camera to new model

        // Extract filename for display
        std::string filename = fs::path(g_models[g_currentModel]).filename().string();
        std::cout << "[gltf-demo] " << filename << std::endl;
    }

    // V key toggles vsync
    if (ctx.key(GLFW_KEY_V).pressed) {
        ctx.vsync(!ctx.vsync());
    }
}

VIVID_CHAIN(setup, update)
