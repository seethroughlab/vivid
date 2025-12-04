// Vivid Example: PBR Test
// Demonstrates PBR materials on a rotating sphere
// Press SPACE to cycle through materials

#include <vivid/vivid.h>
#include <vivid/operators.h>
#include <vivid/mesh.h>
#include <vivid/pbr_material.h>
#include <vivid/ibl.h>
#include <iostream>
#include <vector>
#include <memory>

using namespace vivid;

// Static state that persists across frames
static std::unique_ptr<Render3D> render3d;
static std::unique_ptr<Output> output;
static std::unique_ptr<IBLEnvironment> iblEnv;
static std::vector<PBRMaterial> materials;
static Mesh sphereMesh;
static int sphereIdx = -1;
static int currentMaterial = 0;
static bool initialized = false;

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

    // Create sphere mesh
    MeshData sphereData = MeshUtils::createSphere(64, 32, 1.0f);
    sphereMesh.create(ctx.device(), sphereData);

    // Add to scene
    sphereIdx = render3d->addObject(&sphereMesh, glm::mat4(1.0f));

    // Setup camera
    render3d->camera().setOrbit(glm::vec3(0, 0, 0), 3.5f, 45.0f, 15.0f);

    // Scene settings - brighter background and ambient
    render3d->backgroundColor(0.1f, 0.1f, 0.15f);
    render3d->ambientColor(0.5f, 0.5f, 0.55f);

    // Clear default light and add brighter lights
    render3d->clearLights();

    // Main key light - bright, from upper right front
    Light3D keyLight;
    keyLight.type = Light3D::Type::Directional;
    keyLight.direction = glm::normalize(glm::vec3(-0.5f, -0.8f, -0.5f));
    keyLight.color = glm::vec3(1.0f, 0.98f, 0.95f);
    keyLight.intensity = 3.0f;
    render3d->addLight(keyLight);

    // Fill light - from left, softer
    Light3D fillLight;
    fillLight.type = Light3D::Type::Directional;
    fillLight.direction = glm::normalize(glm::vec3(0.8f, -0.3f, 0.5f));
    fillLight.color = glm::vec3(0.8f, 0.85f, 1.0f);
    fillLight.intensity = 1.5f;
    render3d->addLight(fillLight);

    // Rim light - from behind, for edge definition
    Light3D rimLight;
    rimLight.type = Light3D::Type::Directional;
    rimLight.direction = glm::normalize(glm::vec3(0.0f, -0.5f, 1.0f));
    rimLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
    rimLight.intensity = 2.0f;
    render3d->addLight(rimLight);

    // Set initial material
    currentMaterial = 0;
    if (auto* obj = render3d->getObject(sphereIdx)) {
        obj->material = &materials[currentMaterial];
        obj->uvScale = 2.0f;
        obj->color = glm::vec4(1.0f);
    }

    initialized = true;
    std::cout << "[PBR Test] Ready! Press SPACE to cycle materials." << std::endl;
    std::cout << "Showing: " << materialInfos[currentMaterial].displayName << std::endl;
}

void update(Context& ctx) {
    if (!initialized) return;

    // Check for spacebar to cycle materials
    if (ctx.wasKeyPressed(32)) {  // GLFW_KEY_SPACE = 32
        currentMaterial = (currentMaterial + 1) % static_cast<int>(materials.size());
        if (auto* obj = render3d->getObject(sphereIdx)) {
            obj->material = &materials[currentMaterial];
        }
        std::cout << "Showing: " << materialInfos[currentMaterial].displayName
                  << " (" << (currentMaterial + 1) << "/" << materials.size() << ")" << std::endl;
    }

    // Slowly rotate camera
    render3d->camera().orbitRotate(0.2f, 0.0f);

    // Render
    render3d->process(ctx);
    output->process(ctx);
}

VIVID_CHAIN(setup, update)
