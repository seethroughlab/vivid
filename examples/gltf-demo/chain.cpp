// GLTF Demo - Load and display 3D models from GLTF/GLB files
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
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

// IBL environment (loaded once)
static IBLEnvironment g_ibl;

// Find all .glb files in a directory
std::vector<std::string> findModels(const std::string& directory) {
    std::vector<std::string> models;
    try {
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
    auto& model = chain.add<GLTFLoader>("model")
        .file(g_models[g_currentModel])
        .loadTextures(true)
        .computeTangents(true)
        .scale(1.0f);

    // Create scene
    auto& scene = SceneComposer::create(chain, "scene");
    scene.add(&model, glm::mat4(1.0f), glm::vec4(1.0f));

    // Camera - will be adjusted based on model bounds
    auto& camera = chain.add<CameraOperator>("camera")
        .orbitCenter(0, 0, 0)
        .distance(3.0f)
        .elevation(0.2f)
        .fov(50.0f);

    // Lighting
    auto& sun = chain.add<DirectionalLight>("sun")
        .direction(1, 2, 1)
        .color(1.0f, 0.98f, 0.95f)
        .intensity(2.0f);

    // Load IBL environment from HDR (once)
    if (!g_ibl.isLoaded()) {
        g_ibl.loadHDR(ctx, "assets/hdris/warm_reception_dinner_4k.hdr");
    }

    // Render with PBR + IBL
    auto& render = chain.add<Render3D>("render")
        .input(&scene)
        .cameraInput(&camera)
        .lightInput(&sun)
        .shadingMode(ShadingMode::PBR)
        .ibl(true)
        .environment(&g_ibl)
        .showSkybox(true)
        .metallic(0.0f)
        .roughness(0.5f)
        .clearColor(0.1f, 0.1f, 0.15f);

    chain.output("render");

    std::cout << "[gltf-demo] Found " << g_models.size() << " models" << std::endl;
    std::cout << "[gltf-demo] Press SPACE to cycle models, V to toggle vsync" << std::endl;
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
}

void update(Context& ctx) {
    auto& camera = ctx.chain().get<CameraOperator>("camera");
    auto& model = ctx.chain().get<GLTFLoader>("model");

    // Fit camera on first frame after model loads
    static bool needsFit = true;
    if (model.isLoaded() && needsFit) {
        fitCameraToModel(camera, model.bounds());
        needsFit = false;
    }

    // Slowly orbit camera
    camera.azimuth(ctx.time() * 0.3f);

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
