// 3D Basics - Vivid Example
// Demonstrates node-based geometry workflow with CSG operations
//
// Resolution: Render3D uses its declared resolution() for the render target
// (defaults to 1280x720 if not specified). Output is scaled to window for display.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // SCENE COMPOSER - Entry point for all geometry
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // -------------------------------------------------------------------------
    // Standalone primitives (created via scene.add<T>())
    // -------------------------------------------------------------------------

    auto& torus = scene.add<Torus>("torus",
        glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.0f, 0.0f)),
        glm::vec4(0.9f, 0.4f, 0.8f, 1.0f));  // Pink
    torus.outerRadius(0.5f);
    torus.innerRadius(0.15f);
    torus.segments(32);
    torus.rings(16);

    auto& cylinder = scene.add<Cylinder>("cylinder",
        glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f)),
        glm::vec4(0.3f, 0.9f, 0.4f, 1.0f));  // Green
    cylinder.radius(0.3f);
    cylinder.height(1.5f);
    cylinder.segments(24);
    cylinder.flatShading(true);

    auto& cone = scene.add<Cone>("cone",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 2.5f)),
        glm::vec4(0.9f, 0.7f, 0.2f, 1.0f));  // Orange
    cone.radius(0.4f);
    cone.height(1.0f);
    cone.segments(24);
    cone.flatShading(true);

    // -------------------------------------------------------------------------
    // CSG: Hollow cube (box - sphere)
    // CSG inputs are created via chain.add<>() (not added to scene)
    // -------------------------------------------------------------------------

    auto& box = chain.add<Box>("box");
    box.size(1.2f, 1.2f, 1.2f);
    box.flatShading(true);

    auto& sphere = chain.add<Sphere>("sphere");
    sphere.radius(0.85f);
    sphere.segments(24);

    auto& hollowCube = chain.add<Boolean>("hollowCube");
    hollowCube.setInputA(&box);
    hollowCube.setInputB(&sphere);
    hollowCube.setOperation(BooleanOp::Subtract);
    hollowCube.flatShading = true;

    // Add CSG result to scene
    scene.add(&hollowCube,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
        glm::vec4(0.4f, 0.8f, 1.0f, 1.0f));  // Light blue

    // =========================================================================
    // CAMERA - Required input for 3D rendering
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0.0f, 0.0f, 0.0f);
    camera.distance(8.0f);
    camera.elevation(0.3f);
    camera.azimuth(0.0f);
    camera.fov(50.0f);
    camera.nearPlane(0.1f);
    camera.farPlane(100.0f);

    // =========================================================================
    // RENDER3D - Render scene to texture
    // Set explicit resolution for the render target (1920x1080 for HD output)
    // =========================================================================

    auto& render = chain.add<Render3D>("render3d");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setShadingMode(ShadingMode::Flat);
    render.setLightDirection(glm::normalize(glm::vec3(1, 2, 1)));
    render.setLightColor(glm::vec3(1, 1, 1));
    render.setAmbient(0.2f);
    render.setClearColor(0.08f, 0.08f, 0.12f);
    render.setResolution(1920, 1080);

    chain.output("render3d");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Orbit camera around the scene
    auto& camera = chain.get<CameraOperator>("camera");
    camera.azimuth(time * 0.2f);

    // Animate objects in the scene via SceneComposer
    auto& scene = chain.get<SceneComposer>("scene");
    auto& entries = scene.entries();

    // Torus: spin around multiple axes
    entries[0].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(1, 0, 0));

    // Cylinder: rotate around Y
    entries[1].transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(0, 1, 0));

    // Cone: wobble
    entries[2].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 2.5f)) *
                          glm::rotate(glm::mat4(1.0f), 0.3f * std::sin(time * 1.5f), glm::vec3(1, 0, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(0, 1, 0));

    // Hollow cube: slow rotation to show interior
    entries[3].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.2f, glm::vec3(1, 0, 0));
}

VIVID_CHAIN(setup, update)
