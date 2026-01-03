// GLTF Gallery - Vivid Showcase
// A beautiful 3D model viewer with PBR and IBL lighting
//
// Controls:
//   SPACE: Cycle through models
//   1-5: Select model directly
//   B: Toggle bloom
//   TAB: Open parameter controls

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;
namespace fs = std::filesystem;

// Gallery state
static std::vector<std::string> g_models;
static int g_currentModel = 0;
static bool g_enableBloom = true;

// Smooth camera animation
static float g_currentAzimuth = 0.0f;

std::vector<std::string> findModels(const std::string& directory) {
    std::vector<std::string> models;
    try {
        for (const auto& entry : fs::directory_iterator(directory)) {
            auto ext = entry.path().extension().string();
            if (ext == ".glb" || ext == ".gltf") {
                models.push_back(entry.path().string());
            }
        }
        std::sort(models.begin(), models.end());
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[gallery] Error: " << e.what() << std::endl;
    }
    return models;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Find all models
    g_models = findModels("assets/models");
    if (g_models.empty()) {
        std::cerr << "[gallery] No models found in assets/models/" << std::endl;
        auto& fallback = chain.add<SolidColor>("fallback");
        fallback.color.set(0.2f, 0.1f, 0.15f, 1.0f);  // Dark purple
        chain.output("fallback");
        return;
    }

    // =========================================================================
    // 3D Scene Setup
    // =========================================================================

    // Load model
    auto& model = chain.add<GLTFLoader>("model");
    model.file(g_models[g_currentModel]);
    model.loadTextures(true);
    model.computeTangents(true);
    model.scale = 1.0f;

    // Scene composer
    auto& scene = SceneComposer::create(chain, "scene");
    scene.add(&model, glm::mat4(1.0f), glm::vec4(1.0f));

    // Camera with orbit controls
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0, 0);
    camera.distance(3.0f);
    camera.elevation(0.2f);
    camera.azimuth(0.0f);
    camera.fov(45.0f);

    // Key light (warm, from upper right)
    auto& keyLight = chain.add<DirectionalLight>("keyLight");
    keyLight.direction(-1.0f, -2.0f, -1.5f);  // From upper-right-front
    keyLight.color(1.0f, 0.95f, 0.9f);  // Warm white
    keyLight.intensity = 2.5f;

    // IBL environment for reflections
    auto& ibl = chain.add<IBLEnvironment>("ibl");
    ibl.setHdrFile("assets/warm_reception_dinner_4k.hdr");

    // Main 3D render
    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&keyLight);
    render.setShadingMode(ShadingMode::PBR);
    render.setIbl(true);
    render.setEnvironmentInput(&ibl);
    render.setShowSkybox(true);
    render.setColor(0.08f, 0.06f, 0.1f, 1.0f);  // Dark purple background

    // =========================================================================
    // Post-Processing Effects
    // =========================================================================

    // Bloom for highlights
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.setInput(0, &render);
    bloom.threshold = 0.8f;
    bloom.intensity = 0.4f;
    bloom.radius = 8.0f;

    // Subtle vignette
    auto& vignette = chain.add<CRTEffect>("vignette");
    vignette.setInput(0, &bloom);
    vignette.curvature = 0.0f;
    vignette.vignette = 0.3f;
    vignette.scanlines = 0.0f;
    vignette.bloom = 0.0f;
    vignette.chromatic = 0.0f;

    // Color grading - slight warmth
    auto& colorGrade = chain.add<HSV>("colorGrade");
    colorGrade.setInput(0, &vignette);
    colorGrade.saturation = 1.1f;

    chain.output("colorGrade");

    // =========================================================================
    // Info Display
    // =========================================================================

    std::cout << "\n========================================" << std::endl;
    std::cout << "GLTF Gallery - Vivid Showcase" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Models found: " << g_models.size() << std::endl;
    std::cout << "\nControls:" << std::endl;
    std::cout << "  SPACE / 1-5: Select model" << std::endl;
    std::cout << "  B: Toggle bloom" << std::endl;
    std::cout << "  TAB: Parameters" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void fitCameraToModel(CameraOperator& camera, const Bounds3D& bounds) {
    float radius = bounds.radius();
    float fovRad = glm::radians(45.0f);
    float distance = radius / std::sin(fovRad * 0.5f);
    distance *= 1.8f;
    distance = std::clamp(distance, 1.0f, 50.0f);

    camera.orbitCenter(bounds.center());
    camera.distance(distance);
}

void update(Context& ctx) {
    if (g_models.empty()) return;

    auto& chain = ctx.chain();
    auto& camera = chain.get<CameraOperator>("camera");
    auto& model = chain.get<GLTFLoader>("model");
    auto& bloom = chain.get<Bloom>("bloom");

    // Fit camera when model loads
    static bool needsFit = true;
    if (model.isLoaded() && needsFit) {
        fitCameraToModel(camera, model.bounds());
        needsFit = false;
    }

    // =========================================================================
    // Input Handling
    // =========================================================================

    float dt = static_cast<float>(ctx.dt());

    // Model selection
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        g_currentModel = (g_currentModel + 1) % static_cast<int>(g_models.size());
        model.file(g_models[g_currentModel]);
        needsFit = true;
        std::cout << "[gallery] " << fs::path(g_models[g_currentModel]).filename().string() << std::endl;
    }

    // Direct model selection (1-5)
    for (int i = 0; i < 5 && i < static_cast<int>(g_models.size()); ++i) {
        if (ctx.key(GLFW_KEY_1 + i).pressed) {
            g_currentModel = i;
            model.file(g_models[g_currentModel]);
            needsFit = true;
            std::cout << "[gallery] " << fs::path(g_models[g_currentModel]).filename().string() << std::endl;
        }
    }

    // Toggle bloom
    if (ctx.key(GLFW_KEY_B).pressed) {
        g_enableBloom = !g_enableBloom;
        std::cout << "[gallery] Bloom: " << (g_enableBloom ? "ON" : "OFF") << std::endl;
    }

    // =========================================================================
    // Camera Animation
    // =========================================================================

    // Gentle auto-orbit
    float targetAzimuth = static_cast<float>(ctx.time()) * 0.15f;

    // Subtle elevation wave
    float elevation = 0.15f + std::sin(static_cast<float>(ctx.time()) * 0.3f) * 0.1f;

    // Smooth interpolation
    float smoothing = 1.0f - std::pow(0.001f, dt);
    g_currentAzimuth += (targetAzimuth - g_currentAzimuth) * smoothing;

    camera.azimuth(g_currentAzimuth);
    camera.elevation(elevation);

    // =========================================================================
    // Effect Updates
    // =========================================================================

    bloom.intensity = g_enableBloom ? 0.4f : 0.0f;
}

VIVID_CHAIN(setup, update)
