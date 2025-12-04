// Vivid Example: PBR Test
// Demonstrates PBR materials - all 6 materials shown at once on different spheres
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

using namespace vivid;

// Static state that persists across frames
static std::unique_ptr<Render3D> render3d;
static std::unique_ptr<Output> output;
static std::unique_ptr<IBLEnvironment> iblEnv;
static std::vector<PBRMaterial> materials;
static std::vector<Mesh> sphereMeshes;  // Separate mesh per sphere for separate SRBs
static std::vector<int> sphereIndices;
static float sphereRotation = 0.0f;
static bool initialized = false;

// Focus control
static int currentSphereIndex = -1;  // -1 = overview mode, 0-5 = focused on specific sphere

// Material info
struct MaterialInfo {
    std::string folder;
    std::string prefix;
    std::string displayName;
};

static std::vector<MaterialInfo> materialInfos = {
    {"bronze-bl", "bronze", "Bronze"},
    {"hexagon-pavers1-bl", "hexagon-pavers1", "Hexagon Pavers"},
    {"roughrockface2-bl", "roughrockface2", "Rough Rock Face"},
    {"speckled-granite-tiles-bl", "speckled-granite-tiles", "Speckled Granite Tiles"},
    {"square-damp-blocks-bl", "square-damp-blocks", "Square Damp Blocks"},
    {"whispy-grass-meadow-bl", "wispy-grass-meadow", "Whispy Grass Meadow"}
};

// Sphere positions
static float spacing = 1.1f;
static float startX = -spacing * 2.5f;

glm::vec3 getSpherePosition(int index) {
    return glm::vec3(startX + index * spacing, 0.0f, 0.0f);
}

void updateCamera() {
    if (currentSphereIndex < 0) {
        // Overview mode - see all spheres
        render3d->camera().setOrbit(glm::vec3(0, 0, 0), 6.0f, 90.0f, 15.0f);
    } else {
        // Focus on specific sphere - close up view
        glm::vec3 spherePos = getSpherePosition(currentSphereIndex);
        render3d->camera().setOrbit(spherePos, 1.5f, 90.0f, 10.0f);
    }
}

void setup(Context& ctx) {
    std::cout << "[PBR Test] Setup - initializing..." << std::endl;

    // Asset path
    std::string assetPath = "runtime/vivid.app/Contents/MacOS/assets/";

    // Load materials
    materials.resize(materialInfos.size());
    for (size_t i = 0; i < materialInfos.size(); i++) {
        std::string path = assetPath + "materials/" + materialInfos[i].folder;
        if (!materials[i].loadFromDirectory(ctx, path, materialInfos[i].prefix)) {
            std::cout << "Warning: Could not load " << materialInfos[i].displayName << std::endl;
            materials[i].createDefaults(ctx);
        } else {
            std::cout << "Loaded: " << materialInfos[i].displayName << std::endl;
        }
    }

    // Load IBL environment (HDR for metallic reflections)
    iblEnv = std::make_unique<IBLEnvironment>();
    if (iblEnv->init(ctx)) {
        std::string hdrPath = assetPath + "hdris/bryanston_park_sunrise_4k.hdr";
        if (iblEnv->loadHDR(ctx, hdrPath)) {
            std::cout << "Loaded IBL environment: " << hdrPath << std::endl;
        } else {
            std::cout << "Warning: Could not load IBL HDR, metallic reflections may not work" << std::endl;
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
        std::cout << "IBL environment connected to Render3D" << std::endl;
    }

    // Create separate mesh for each sphere (needed for separate material bindings)
    MeshData sphereData = MeshUtils::createSphere(64, 32, 0.45f);  // Small spheres
    MeshUtils::calculateTangents(sphereData);

    sphereMeshes.resize(6);
    for (int i = 0; i < 6; i++) {
        sphereMeshes[i].create(ctx.device(), sphereData);
    }

    // Add 6 spheres in a single row for easy comparison
    sphereIndices.resize(6);
    for (int i = 0; i < 6; i++) {
        glm::vec3 pos = getSpherePosition(i);
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos);
        sphereIndices[i] = render3d->addObject(&sphereMeshes[i], transform);

        if (auto* obj = render3d->getObject(sphereIndices[i])) {
            obj->material = &materials[i];
            obj->uvScale = 2.0f;
            obj->color = glm::vec4(1.0f);
            std::cout << "Sphere " << i << " assigned material: " << materialInfos[i].displayName << std::endl;
        }
    }

    // Setup camera - start in overview mode
    currentSphereIndex = -1;
    updateCamera();

    // Scene settings
    render3d->backgroundColor(0.1f, 0.1f, 0.15f);
    render3d->ambientColor(0.5f, 0.5f, 0.55f);

    // Clear default light and add brighter lights
    render3d->clearLights();

    // Main key light
    Light3D keyLight;
    keyLight.type = Light3D::Type::Directional;
    keyLight.direction = glm::normalize(glm::vec3(-0.5f, -0.8f, -0.5f));
    keyLight.color = glm::vec3(1.0f, 0.98f, 0.95f);
    keyLight.intensity = 3.0f;
    render3d->addLight(keyLight);

    // Fill light
    Light3D fillLight;
    fillLight.type = Light3D::Type::Directional;
    fillLight.direction = glm::normalize(glm::vec3(0.8f, -0.3f, 0.5f));
    fillLight.color = glm::vec3(0.8f, 0.85f, 1.0f);
    fillLight.intensity = 1.5f;
    render3d->addLight(fillLight);

    // Rim light
    Light3D rimLight;
    rimLight.type = Light3D::Type::Directional;
    rimLight.direction = glm::normalize(glm::vec3(0.0f, -0.5f, 1.0f));
    rimLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
    rimLight.intensity = 2.0f;
    render3d->addLight(rimLight);

    initialized = true;
    std::cout << "[PBR Test] Ready! Press SPACE to cycle between spheres." << std::endl;
    std::cout << "Materials: Bronze, Hexagon, Rock, Granite, Blocks, Grass" << std::endl;
}

void update(Context& ctx) {
    if (!initialized) return;

    // Check for spacebar press (wasKeyPressed handles debouncing)
    // GLFW_KEY_SPACE = 32
    if (ctx.wasKeyPressed(32)) {
        // Cycle to next sphere (-1 -> 0 -> 1 -> ... -> 5 -> -1)
        currentSphereIndex++;
        if (currentSphereIndex >= 6) {
            currentSphereIndex = -1;
        }

        if (currentSphereIndex < 0) {
            std::cout << "[View] Overview - all 6 materials" << std::endl;
        } else {
            std::cout << "[View] " << materialInfos[currentSphereIndex].displayName
                      << " (sphere " << currentSphereIndex << ")" << std::endl;
        }

        updateCamera();
    }

    // Slowly rotate all spheres
    sphereRotation += 0.5f * ctx.dt();

    for (int i = 0; i < 6; i++) {
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
