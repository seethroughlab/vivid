// Vivid Example: PBR Test
// Demonstrates all PBR materials from assets/materials displayed on spheres
// Press SPACE to cycle between spheres for close-up view

#include <vivid/vivid.h>
#include <vivid/operators.h>
#include <vivid/mesh.h>
#include <vivid/pbr_material.h>
#include <vivid/ibl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>
#include <memory>
#include <filesystem>

using namespace vivid;
namespace fs = std::filesystem;

// Static state that persists across frames
static std::unique_ptr<Render3D> render3d;
static std::unique_ptr<Output> output;
static std::unique_ptr<IBLEnvironment> iblEnv;
static std::vector<PBRMaterial> materials;
static std::vector<Mesh> sphereMeshes;
static std::vector<int> sphereIndices;
static float sphereRotation = 0.0f;
static bool initialized = false;

// Focus control
static int currentSphereIndex = -1;  // -1 = overview mode, 0+ = focused on specific sphere

// Material info
struct MaterialInfo {
    std::string folder;
    std::string prefix;  // Empty = auto-detect from folder name
    std::string displayName;
};

// All materials from assets/materials with their prefixes
// Some materials have inconsistent naming, so we specify prefixes explicitly
static std::vector<MaterialInfo> materialInfos = {
    {"alien-panels-bl", "alien-panels", "Alien Panels"},
    {"bronze-bl", "bronze", "Bronze"},
    {"cheap-plywood1-bl", "cheap_plywood1r", "Cheap Plywood"},
    {"corkboard3b-bl", "corkboard3b", "Corkboard"},
    {"cracking-painted-asphalt1-bl", "cracking_painted_asphalt", "Cracked Asphalt"},
    {"futuristic-hex-armor-bl", "futuristic-hex-armor", "Hex Armor"},
    {"hammered-gold-bl", "hammered-gold", "Hammered Gold"},
    {"hexagon-pavers1-bl", "hexagon-pavers1", "Hexagon Pavers"},
    {"metal-roof-bl", "metal-roof", "Metal Roof"},
    {"metal-slpotchy-bl", "metal-splotchy", "Metal Splotchy"},
    {"oily-tubework-bl", "oily-tubework", "Oily Tubework"},
    {"plywood1-bl", "plywood", "Plywood"},
    {"roughrockface2-bl", "roughrockface2", "Rough Rock"},
    {"speckled-granite-tiles-bl", "speckled-granite-tiles", "Granite Tiles"},
    {"square-damp-blocks-bl", "square-damp-blocks", "Damp Blocks"},
    {"Titanium-Scuffed-bl", "Titanium-Scuffed", "Titanium Scuffed"},
    {"whispy-grass-meadow-bl", "wispy-grass-meadow", "Grass Meadow"},
    {"worn-rusted-painted-bl", "worn-rusted-painted", "Rusted Painted"},
    {"worn-shiny-metal-bl", "worn-shiny-metal", "Worn Metal"}
};

// Grid layout: 5 columns x 4 rows
static const int GRID_COLS = 5;
static const int GRID_ROWS = 4;
static const float SPHERE_SPACING = 1.2f;

glm::vec3 getSpherePosition(int index) {
    int row = index / GRID_COLS;
    int col = index % GRID_COLS;

    // Center the grid
    float totalWidth = (GRID_COLS - 1) * SPHERE_SPACING;
    float totalHeight = (GRID_ROWS - 1) * SPHERE_SPACING;

    float x = col * SPHERE_SPACING - totalWidth * 0.5f;
    float y = -row * SPHERE_SPACING + totalHeight * 0.5f;  // Top to bottom

    return glm::vec3(x, y, 0.0f);
}

void updateCamera() {
    int numMaterials = static_cast<int>(materialInfos.size());

    if (currentSphereIndex < 0) {
        // Overview mode - see all spheres
        // Calculate optimal distance to see entire grid
        float gridWidth = (GRID_COLS - 1) * SPHERE_SPACING + 1.0f;
        float gridHeight = (GRID_ROWS - 1) * SPHERE_SPACING + 1.0f;
        float maxExtent = std::max(gridWidth, gridHeight);
        float distance = maxExtent * 1.2f;

        render3d->camera().setOrbit(glm::vec3(0, 0, 0), distance, 90.0f, 0.0f);
    } else if (currentSphereIndex < numMaterials) {
        // Focus on specific sphere - close up view
        glm::vec3 spherePos = getSpherePosition(currentSphereIndex);
        render3d->camera().setOrbit(spherePos, 1.4f, 90.0f, 5.0f);
    }
}

void setup(Context& ctx) {
    std::cout << "[PBR Test] Setup - loading " << materialInfos.size() << " materials..." << std::endl;

    // Asset path
    std::string assetPath = "runtime/vivid.app/Contents/MacOS/assets/";

    // Check if running from build directory directly
    if (!fs::exists(assetPath)) {
        assetPath = "build/runtime/assets/";
    }
    if (!fs::exists(assetPath)) {
        assetPath = "assets/";
    }

    // Load all materials
    materials.resize(materialInfos.size());
    int loadedCount = 0;
    for (size_t i = 0; i < materialInfos.size(); i++) {
        std::string path = assetPath + "materials/" + materialInfos[i].folder;
        if (!materials[i].loadFromDirectory(ctx, path, materialInfos[i].prefix)) {
            std::cout << "  [!] Could not load " << materialInfos[i].displayName << std::endl;
            materials[i].createDefaults(ctx);
        } else {
            loadedCount++;
        }
    }
    std::cout << "[PBR Test] Loaded " << loadedCount << "/" << materialInfos.size() << " materials" << std::endl;

    // Load IBL environment (HDR for metallic reflections)
    iblEnv = std::make_unique<IBLEnvironment>();
    if (iblEnv->init(ctx)) {
        std::string hdrPath = assetPath + "hdris/bryanston_park_sunrise_4k.hdr";
        if (iblEnv->loadHDR(ctx, hdrPath)) {
            std::cout << "[PBR Test] Loaded IBL environment" << std::endl;
        }
    }

    // Create operators
    render3d = std::make_unique<Render3D>();
    output = std::make_unique<Output>();
    output->setInput(render3d.get());

    render3d->init(ctx);
    output->init(ctx);

    // Set IBL environment for metallic reflections
    if (iblEnv && iblEnv->isLoaded()) {
        render3d->setEnvironment(iblEnv.get());
    }

    // Create sphere mesh data once
    MeshData sphereData = MeshUtils::createSphere(48, 24, 0.45f);
    MeshUtils::calculateTangents(sphereData);

    // Create separate mesh for each sphere (needed for separate material bindings)
    int numMaterials = static_cast<int>(materialInfos.size());
    sphereMeshes.resize(numMaterials);
    sphereIndices.resize(numMaterials);

    for (int i = 0; i < numMaterials; i++) {
        sphereMeshes[i].create(ctx.device(), sphereData);

        glm::vec3 pos = getSpherePosition(i);
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos);
        sphereIndices[i] = render3d->addObject(&sphereMeshes[i], transform);

        if (auto* obj = render3d->getObject(sphereIndices[i])) {
            obj->material = &materials[i];
            obj->uvScale = 2.0f;
            obj->color = glm::vec4(1.0f);
        }
    }

    // Setup camera - start in overview mode
    currentSphereIndex = -1;
    updateCamera();

    // Scene settings
    render3d->backgroundColor(0.08f, 0.08f, 0.1f);
    render3d->ambientColor(0.4f, 0.4f, 0.45f);

    // Clear default light and add studio lighting
    render3d->clearLights();

    // Main key light (warm)
    Light3D keyLight;
    keyLight.type = Light3D::Type::Directional;
    keyLight.direction = glm::normalize(glm::vec3(-0.5f, -0.8f, -0.5f));
    keyLight.color = glm::vec3(1.0f, 0.95f, 0.9f);
    keyLight.intensity = 2.5f;
    render3d->addLight(keyLight);

    // Fill light (cool)
    Light3D fillLight;
    fillLight.type = Light3D::Type::Directional;
    fillLight.direction = glm::normalize(glm::vec3(0.8f, -0.3f, 0.5f));
    fillLight.color = glm::vec3(0.8f, 0.85f, 1.0f);
    fillLight.intensity = 1.2f;
    render3d->addLight(fillLight);

    // Rim light
    Light3D rimLight;
    rimLight.type = Light3D::Type::Directional;
    rimLight.direction = glm::normalize(glm::vec3(0.0f, -0.5f, 1.0f));
    rimLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
    rimLight.intensity = 1.5f;
    render3d->addLight(rimLight);

    initialized = true;

    std::cout << "\n[PBR Test] Ready!" << std::endl;
    std::cout << "  Press SPACE to cycle through materials" << std::endl;
    std::cout << "  Materials arranged in " << GRID_COLS << "x" << GRID_ROWS << " grid\n" << std::endl;
}

void update(Context& ctx) {
    if (!initialized) return;

    int numMaterials = static_cast<int>(materialInfos.size());

    // Check for spacebar press (GLFW_KEY_SPACE = 32)
    if (ctx.wasKeyPressed(32)) {
        // Cycle to next sphere (-1 -> 0 -> 1 -> ... -> N-1 -> -1)
        currentSphereIndex++;
        if (currentSphereIndex >= numMaterials) {
            currentSphereIndex = -1;
        }

        if (currentSphereIndex < 0) {
            std::cout << "[View] Overview - all " << numMaterials << " materials" << std::endl;
        } else {
            std::cout << "[View] " << materialInfos[currentSphereIndex].displayName
                      << " (" << (currentSphereIndex + 1) << "/" << numMaterials << ")" << std::endl;
        }

        updateCamera();
    }

    // Slowly rotate all spheres
    sphereRotation += 0.3f * ctx.dt();

    for (int i = 0; i < numMaterials; i++) {
        glm::vec3 pos = getSpherePosition(i);

        if (auto* obj = render3d->getObject(sphereIndices[i])) {
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos);
            transform = glm::rotate(transform, sphereRotation, glm::vec3(0, 1, 0));
            obj->transform = transform;
        }
    }

    // Render
    render3d->process(ctx);
    output->process(ctx);
}

VIVID_CHAIN(setup, update)
