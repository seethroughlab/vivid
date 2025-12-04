// Vivid Example: Mesh Primitives Test
// Demonstrates all built-in mesh utilities: cube, sphere, plane, cylinder, torus, cone
// Press SPACE to cycle through primitives, drag mouse to rotate camera

#include <vivid/vivid.h>
#include <vivid/operators.h>
#include <vivid/mesh.h>
#include <vivid/pbr_material.h>
#include <vivid/ibl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>

using namespace vivid;

// Static state
static std::unique_ptr<Render3D> render3d;
static std::unique_ptr<Output> output;
static std::unique_ptr<IBLEnvironment> iblEnv;
static std::vector<PBRMaterial> materials;
static std::vector<Mesh> meshes;
static std::vector<int> objectIndices;
static bool initialized = false;

// Mesh info
struct MeshInfo {
    const char* name;
    const char* materialFolder;
    const char* materialPrefix;
};

static MeshInfo meshInfos[] = {
    {"Cube",     "bronze-bl",                "bronze"},
    {"Sphere",   "roughrockface2-bl",        "roughrockface2"},
    {"Plane",    "hexagon-pavers1-bl",       "hexagon-pavers1"},
    {"Cylinder", "speckled-granite-tiles-bl","speckled-granite-tiles"},
    {"Torus",    "square-damp-blocks-bl",    "square-damp-blocks"},
    {"Cone",     "whispy-grass-meadow-bl",   "wispy-grass-meadow"}
};

// Current view mode
static int currentMesh = -1;  // -1 = show all, 0-5 = single mesh
static float rotation = 0.0f;

// Mouse interaction
static glm::vec2 lastMousePos{0.0f, 0.0f};
static bool isDragging = false;

// Mesh positions for overview (2 rows of 3)
glm::vec3 getMeshPosition(int index) {
    int row = index / 3;
    int col = index % 3;
    float spacing = 2.0f;
    return glm::vec3((col - 1) * spacing, (row == 0 ? 1.0f : -1.0f), 0.0f);
}

void updateCamera() {
    if (currentMesh < 0) {
        // Overview - see all meshes
        render3d->camera().setOrbit(glm::vec3(0, 0, 0), 6.0f, 90.0f, 20.0f);
    } else {
        // Focus on single mesh
        glm::vec3 pos = (currentMesh < 0) ? glm::vec3(0) : getMeshPosition(currentMesh);
        render3d->camera().setOrbit(pos, 3.0f, 90.0f, 15.0f);
    }
}

void setup(Context& ctx) {
    std::cout << "[Mesh Test] Setup - initializing..." << std::endl;

    // Asset path
    std::string assetPath = "build/runtime/vivid.app/Contents/MacOS/assets/";

    // Load PBR materials
    materials.resize(6);
    for (int i = 0; i < 6; i++) {
        std::string path = assetPath + "materials/" + meshInfos[i].materialFolder;
        if (!materials[i].loadFromDirectory(ctx, path, meshInfos[i].materialPrefix)) {
            std::cout << "[Mesh Test] Warning: Could not load " << meshInfos[i].name << " material" << std::endl;
            materials[i].createDefaults(ctx);
        } else {
            std::cout << "[Mesh Test] Loaded material: " << meshInfos[i].materialFolder << std::endl;
        }
    }

    // Load IBL environment
    iblEnv = std::make_unique<IBLEnvironment>();
    if (iblEnv->init(ctx)) {
        std::string hdrPath = assetPath + "hdris/bryanston_park_sunrise_4k.hdr";
        if (iblEnv->loadHDR(ctx, hdrPath)) {
            std::cout << "[Mesh Test] IBL environment loaded" << std::endl;
        }
    }

    // Create operators
    render3d = std::make_unique<Render3D>();
    output = std::make_unique<Output>();
    output->setInput(render3d.get());

    render3d->init(ctx);
    output->init(ctx);

    if (iblEnv && iblEnv->isLoaded()) {
        render3d->setEnvironment(iblEnv.get());
    }

    // Create all mesh primitives
    meshes.resize(6);
    objectIndices.resize(6);

    // 0: Cube
    {
        MeshData data = MeshUtils::createCube();
        meshes[0].create(ctx.device(), data);
    }

    // 1: Sphere (higher res for better material display)
    {
        MeshData data = MeshUtils::createSphere(48, 24, 0.5f);
        MeshUtils::calculateTangents(data);
        meshes[1].create(ctx.device(), data);
    }

    // 2: Plane
    {
        MeshData data = MeshUtils::createPlane(1.5f, 1.5f, 1, 1);
        meshes[2].create(ctx.device(), data);
    }

    // 3: Cylinder
    {
        MeshData data = MeshUtils::createCylinder(48, 0.4f, 1.0f);
        meshes[3].create(ctx.device(), data);
    }

    // 4: Torus
    {
        MeshData data = MeshUtils::createTorus(48, 24, 0.4f, 0.15f);
        meshes[4].create(ctx.device(), data);
    }

    // 5: Cone
    {
        MeshData data = MeshUtils::createCone(48, 0.4f, 1.0f);
        meshes[5].create(ctx.device(), data);
    }

    // Add all meshes to scene with their materials
    for (int i = 0; i < 6; i++) {
        glm::vec3 pos = getMeshPosition(i);
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos);
        objectIndices[i] = render3d->addObject(&meshes[i], transform);

        if (auto* obj = render3d->getObject(objectIndices[i])) {
            obj->material = &materials[i];
            obj->uvScale = 2.0f;  // Tile textures for better visibility
            obj->color = glm::vec4(1.0f);  // Full brightness
        }
    }

    // Setup camera
    currentMesh = -1;
    updateCamera();

    // Scene settings
    render3d->backgroundColor(0.1f, 0.1f, 0.15f);
    render3d->ambientColor(0.4f, 0.4f, 0.45f);

    // Setup lights - demonstrate all 3 types
    render3d->clearLights();

    // Key light - directional (sun-like)
    render3d->addLight(Light3D::directional(
        glm::vec3(-0.5f, -0.8f, -0.5f),  // direction
        3.0f,                             // intensity
        glm::vec3(1.0f, 0.98f, 0.95f)    // warm white
    ));

    // Fill light - directional (sky fill)
    render3d->addLight(Light3D::directional(
        glm::vec3(0.8f, -0.3f, 0.5f),
        1.2f,
        glm::vec3(0.7f, 0.8f, 1.0f)      // cool blue
    ));

    // Accent light - point light (adds local highlights)
    render3d->addLight(Light3D::point(
        glm::vec3(2.0f, 2.0f, 3.0f),     // position
        80.0f,                            // intensity
        8.0f,                             // range
        glm::vec3(1.0f, 0.9f, 0.7f)      // warm
    ));

    // Spot light - focused beam
    render3d->addLight(Light3D::spot(
        glm::vec3(-2.0f, 3.0f, 2.0f),    // position
        glm::vec3(0.5f, -0.7f, -0.5f),   // direction
        150.0f,                           // intensity
        0.2f,                             // inner cone ~11 deg
        0.4f,                             // outer cone ~23 deg
        10.0f,                            // range
        glm::vec3(0.9f, 0.95f, 1.0f)     // cool white
    ));

    initialized = true;
    std::cout << "\n[Mesh Test] Ready! " << render3d->lightCount() << " lights active." << std::endl;
    std::cout << "  Lights: 2 directional, 1 point, 1 spot" << std::endl;
    std::cout << "  Press SPACE to cycle through meshes" << std::endl;
    std::cout << "  Press 1-6 to view specific mesh" << std::endl;
    std::cout << "  Press 0 to view all meshes" << std::endl;
    std::cout << "  Drag mouse to rotate camera, scroll to zoom" << std::endl;
    std::cout << "\nMeshes: Cube(bronze), Sphere(rock), Plane(hexagon)," << std::endl;
    std::cout << "        Cylinder(granite), Torus(blocks), Cone(grass)" << std::endl;
}

void update(Context& ctx) {
    if (!initialized) return;

    // Check for key presses
    // '0' = 48, '1' = 49, ... '6' = 54
    int newMesh = currentMesh;

    if (ctx.wasKeyPressed(48)) newMesh = -1;  // '0' - show all
    if (ctx.wasKeyPressed(49)) newMesh = 0;   // '1' - cube
    if (ctx.wasKeyPressed(50)) newMesh = 1;   // '2' - sphere
    if (ctx.wasKeyPressed(51)) newMesh = 2;   // '3' - plane
    if (ctx.wasKeyPressed(52)) newMesh = 3;   // '4' - cylinder
    if (ctx.wasKeyPressed(53)) newMesh = 4;   // '5' - torus
    if (ctx.wasKeyPressed(54)) newMesh = 5;   // '6' - cone

    // SPACE (32) cycles through
    if (ctx.wasKeyPressed(32)) {
        newMesh = (currentMesh + 2) % 7 - 1;  // -1, 0, 1, 2, 3, 4, 5, -1, ...
    }

    if (newMesh != currentMesh) {
        currentMesh = newMesh;
        updateCamera();

        if (currentMesh < 0) {
            std::cout << "[Mesh Test] Viewing: All primitives" << std::endl;
        } else {
            std::cout << "[Mesh Test] Focusing on: " << meshInfos[currentMesh].name
                      << " (" << meshInfos[currentMesh].materialFolder << ")" << std::endl;
        }
    }

    // Rotate meshes slowly
    rotation += 0.5f * ctx.dt();

    for (int i = 0; i < 6; i++) {
        if (auto* obj = render3d->getObject(objectIndices[i])) {
            glm::vec3 pos = getMeshPosition(i);
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos);
            transform = glm::rotate(transform, rotation, glm::vec3(0, 1, 0));

            // Tilt plane so we can see it better
            if (i == 2) {
                transform = glm::rotate(transform, -0.5f, glm::vec3(1, 0, 0));
            }

            obj->transform = transform;
        }
    }

    // Mouse camera control
    glm::vec2 mousePos = ctx.mousePosition();

    if (ctx.isMouseDown(0)) {
        if (isDragging) {
            glm::vec2 delta = mousePos - lastMousePos;
            float sensitivity = 0.3f;
            render3d->camera().orbitRotate(delta.x * sensitivity, delta.y * sensitivity);
        }
        isDragging = true;
    } else {
        isDragging = false;
    }
    lastMousePos = mousePos;

    // Scroll wheel zoom
    glm::vec2 scroll = ctx.scrollDelta();
    if (std::abs(scroll.y) > 0.01f) {
        float zoomFactor = 1.0f - scroll.y * 0.1f;
        render3d->camera().orbitZoom(zoomFactor);
    }

    // Render
    render3d->process(ctx);
    output->process(ctx);
}

VIVID_CHAIN(setup, update)
