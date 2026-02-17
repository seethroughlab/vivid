// Render3D Demo - Vivid Example
// Demonstrates 3D rendering with procedural geometry and CSG operations
// Using the SceneComposer API for chain visualizer integration

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create SceneComposer - manages geometry lifecycle and chain registration
    auto& scene = SceneComposer::create(chain, "scene");

    // Add custom meshes via addMesh() - they appear in the chain visualizer

    // Simple cube with flat shading
    auto cubeBuilder = MeshBuilder::box(1.0f, 1.0f, 1.0f);
    cubeBuilder.computeFlatNormals();
    scene.addMesh("cube", cubeBuilder,
        glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.0f, 0.0f)),
        glm::vec4(1.0f, 0.4f, 0.3f, 1.0f));

    // Smooth sphere
    auto sphereBuilder = MeshBuilder::sphere(0.5f, 24);
    scene.addMesh("sphere", sphereBuilder,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
        glm::vec4(0.3f, 0.6f, 1.0f, 1.0f));

    // CSG: Cube with spherical hole (using MeshBuilder CSG)
    auto csgBuilder = MeshBuilder::box(1.5f, 1.5f, 1.5f);
    csgBuilder.subtract(MeshBuilder::sphere(1.0f, 24));
    csgBuilder.computeFlatNormals();
    scene.addMesh("csg", csgBuilder,
        glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f)),
        glm::vec4(0.4f, 1.0f, 0.5f, 1.0f));

    // Set up camera
    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(5.0f, 3.0f, 5.0f);
    camera.target(0.0f, 0.0f, 0.0f);
    camera.fov(45.0f);
    camera.nearPlane(0.1f);
    camera.farPlane(100.0f);

    // Directional light
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(1.0f, 2.0f, 1.0f);
    sun.color(1.0f, 1.0f, 1.0f);
    sun.intensity = 1.0f;

    // Create chain: SceneComposer -> Render3D -> ChromaticAberration -> output
    auto& renderer = chain.add<Render3D>("render3d");
    renderer.input("scene");
    renderer.setCameraInput(&camera);
    renderer.setLightInput(&sun);
    renderer.setShadingMode(ShadingMode::Flat);
    renderer.setAmbient(0.15f);
    renderer.setClearColor(0.05f, 0.05f, 0.1f);
    renderer.setResolution(1280, 720);

    // Add chromatic aberration effect
    auto& chromatic = chain.add<ChromaticAberration>("chromatic");
    chromatic.input("render3d");
    chromatic.amount = 0.008f;
    chromatic.radial = true;

    chain.output("chromatic");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Orbit camera around the scene
    auto& camera = chain.get<CameraOperator>("camera");
    camera.orbitCenter(0.0f, 0.0f, 0.0f);
    camera.distance(7.0f);
    camera.azimuth(time * 0.3f);
    camera.elevation(0.4f + 0.1f * std::sin(time * 0.5f));

    // Animate objects via SceneComposer entries
    auto& scene = chain.get<SceneComposer>("scene");
    auto& entries = scene.entries();

    // Rotate cube around Y axis
    entries[0].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0));

    // Bob sphere up and down
    float y = 0.3f * std::sin(time * 2.0f);
    entries[1].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, y, 0.0f));

    // Rotate CSG shape
    entries[2].transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.2f, glm::vec3(1, 0, 0));
}

VIVID_CHAIN(setup, update)
