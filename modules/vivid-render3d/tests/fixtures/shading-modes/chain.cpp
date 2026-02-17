// Shading Modes Demo - Vivid Example
// Demonstrates all shading modes: Unlit, Flat, Gouraud, VertexLit, Toon, PBR
// Press SPACE to cycle through shading modes

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <GLFW/glfw3.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Persistent state across hot-reloads
static int currentMode = 0;

// Shading mode names for display
static const char* modeNames[] = {
    "Unlit",
    "Flat",
    "Gouraud",
    "VertexLit",
    "Toon",
    "PBR"
};

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create SceneComposer - manages geometry lifecycle
    auto& scene = SceneComposer::create(chain, "scene");

    // Add variety of geometry to showcase shading differences

    // 1. Faceted cube (flat normals) - shows flat vs smooth shading difference
    auto cubeBuilder = MeshBuilder::box(1.2f, 1.2f, 1.2f);
    cubeBuilder.computeFlatNormals();
    scene.addMesh("cube", cubeBuilder,
        glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.0f, 0.0f)),
        glm::vec4(1.0f, 0.4f, 0.3f, 1.0f));

    // 2. Smooth sphere - shows lighting quality differences
    auto sphereBuilder = MeshBuilder::sphere(0.7f, 32);
    scene.addMesh("sphere", sphereBuilder,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
        glm::vec4(0.3f, 0.7f, 1.0f, 1.0f));

    // 3. Low-poly sphere - shows Gouraud vs Flat difference clearly
    auto lowPolySphere = MeshBuilder::sphere(0.7f, 8);
    scene.addMesh("lowpoly", lowPolySphere,
        glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)),
        glm::vec4(0.5f, 1.0f, 0.5f, 1.0f));

    // 4. Torus - shows curved surface shading
    auto torusBuilder = MeshBuilder::torus(0.5f, 0.2f, 24, 12);
    scene.addMesh("torus", torusBuilder,
        glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.0f, 2.5f)),
        glm::vec4(1.0f, 0.8f, 0.2f, 1.0f));

    // 5. Cylinder - shows flat caps vs smooth sides
    auto cylinderBuilder = MeshBuilder::cylinder(0.4f, 1.2f, 16);
    scene.addMesh("cylinder", cylinderBuilder,
        glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.0f, 2.5f)),
        glm::vec4(0.8f, 0.3f, 0.8f, 1.0f));

    // 6. CSG object - complex geometry
    auto csgBuilder = MeshBuilder::box(1.0f, 1.0f, 1.0f);
    csgBuilder.subtract(MeshBuilder::sphere(0.7f, 16));
    csgBuilder.computeFlatNormals();
    scene.addMesh("csg", csgBuilder,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.5f)),
        glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));

    // Set up camera
    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(6.0f, 4.0f, 6.0f);
    camera.target(0.0f, 0.0f, 0.0f);
    camera.fov(45.0f);
    camera.nearPlane(0.1f);
    camera.farPlane(100.0f);

    // Directional light
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(1.0f, 2.0f, 1.0f);
    sun.color(1.0f, 1.0f, 1.0f);
    sun.intensity = 1.0f;

    // Create Render3D with initial shading mode
    auto& renderer = chain.add<Render3D>("render3d");
    renderer.input("scene");
    renderer.setCameraInput(&camera);
    renderer.setLightInput(&sun);
    renderer.setShadingMode(ShadingMode::Flat);
    renderer.setAmbient(0.15f);
    renderer.setToonLevels(4);
    renderer.setMetallic(0.0f);
    renderer.setRoughness(0.5f);
    renderer.setClearColor(0.08f, 0.08f, 0.12f);
    renderer.setResolution(1280, 720);

    chain.output("render3d");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Check for SPACE key to cycle shading modes
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        currentMode = (currentMode + 1) % 6;
        printf("Shading Mode: %s\n", modeNames[currentMode]);
    }

    // Orbit camera around the scene
    auto& camera = chain.get<CameraOperator>("camera");
    camera.orbitCenter(0.0f, 0.0f, 0.0f);
    camera.distance(8.0f);
    camera.azimuth(time * 0.2f);
    camera.elevation(0.4f + 0.1f * std::sin(time * 0.3f));

    // Update renderer with current shading mode
    auto& renderer = chain.get<Render3D>("render3d");

    // Apply the current shading mode
    switch (currentMode) {
        case 0: renderer.setShadingMode(ShadingMode::Unlit); break;
        case 1: renderer.setShadingMode(ShadingMode::Flat); break;
        case 2: renderer.setShadingMode(ShadingMode::Gouraud); break;
        case 3: renderer.setShadingMode(ShadingMode::VertexLit); break;
        case 4: renderer.setShadingMode(ShadingMode::Toon); break;
        case 5: renderer.setShadingMode(ShadingMode::PBR); break;
    }

    // Animate objects
    auto& scene = chain.get<SceneComposer>("scene");
    auto& entries = scene.entries();

    // Rotate cube
    entries[0].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(0, 1, 0));

    // Rotate smooth sphere
    entries[1].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(0, 1, 0));

    // Rotate low-poly sphere
    entries[2].transform = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0));

    // Rotate torus
    entries[3].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.0f, 2.5f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.6f, glm::vec3(1, 0, 0));

    // Rotate cylinder
    entries[4].transform = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.0f, 2.5f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(0, 1, 1));

    // Rotate CSG
    entries[5].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.5f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.2f, glm::vec3(1, 0, 0));
}

VIVID_CHAIN(setup, update)
