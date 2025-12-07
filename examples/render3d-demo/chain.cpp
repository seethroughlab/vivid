// Render3D Demo - Vivid Example
// Demonstrates 3D rendering with procedural geometry and CSG operations

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Persistent state across hot-reloads (these don't need Chain management)
static Camera3D camera;
static Scene scene;
static Mesh cubeMesh;
static Mesh sphereMesh;
static Mesh csgMesh;

void setup(Context& ctx) {
    auto& chain = ctx.chain();
    scene.clear();

    // Create procedural geometry

    // Simple cube
    auto cubeBuilder = MeshBuilder::box(1.0f, 1.0f, 1.0f);
    cubeBuilder.computeFlatNormals();  // Faceted look
    cubeMesh = cubeBuilder.build();
    cubeMesh.upload(ctx);

    // Sphere
    auto sphereBuilder = MeshBuilder::sphere(0.5f, 24);
    sphereMesh = sphereBuilder.build();
    sphereMesh.upload(ctx);

    // CSG: Cube with spherical hole
    auto csgCube = MeshBuilder::box(1.5f, 1.5f, 1.5f);
    auto csgSphere = MeshBuilder::sphere(1.0f, 24);
    csgCube.subtract(csgSphere);
    csgCube.computeFlatNormals();
    csgMesh = csgCube.build();
    csgMesh.upload(ctx);

    // Set up scene with multiple objects
    scene.add(cubeMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.0f, 0.0f)),
              glm::vec4(1.0f, 0.4f, 0.3f, 1.0f));

    scene.add(sphereMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
              glm::vec4(0.3f, 0.6f, 1.0f, 1.0f));

    // Add CSG mesh
    scene.add(csgMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f)),
              glm::vec4(0.4f, 1.0f, 0.5f, 1.0f));

    // Set up camera
    camera.lookAt(glm::vec3(5, 3, 5), glm::vec3(0, 0, 0))
          .fov(45.0f)
          .nearPlane(0.1f)
          .farPlane(100.0f);

    // Create chain: Render3D -> ChromaticAberration -> output
    auto& renderer = chain.add<Render3D>("render3d");
    renderer.scene(scene)
            .camera(camera)
            .shadingMode(ShadingMode::Flat)
            .lightDirection(glm::normalize(glm::vec3(1, 2, 1)))
            .lightColor(glm::vec3(1, 1, 1))
            .ambient(0.15f)
            .clearColor(0.05f, 0.05f, 0.1f)
            .resolution(1280, 720);

    // Add chromatic aberration effect
    auto& chromatic = chain.add<ChromaticAberration>("chromatic");
    chromatic.input(&renderer)
             .amount(0.008f)
             .radial(true);

    chain.output("chromatic");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Orbit camera around the scene
    float distance = 7.0f;
    float azimuth = time * 0.3f;
    float elevation = 0.4f + 0.1f * std::sin(time * 0.5f);
    camera.orbit(distance, azimuth, elevation);

    auto& renderer = chain.get<Render3D>("render3d");
    renderer.camera(camera);

    // Update object transforms - rotate each mesh
    auto& objects = scene.objects();

    // Rotate cube around Y axis
    objects[0].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0));

    // Bob sphere up and down
    float y = 0.3f * std::sin(time * 2.0f);
    objects[1].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, y, 0.0f));

    // Rotate CSG shape (only if CSG mesh is in scene)
    if (objects.size() > 2) {
        objects[2].transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f)) *
                              glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(0, 1, 0)) *
                              glm::rotate(glm::mat4(1.0f), time * 0.2f, glm::vec3(1, 0, 0));
    }
}

VIVID_CHAIN(setup, update)
