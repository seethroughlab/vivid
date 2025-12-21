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

    scene.add<Torus>("torus",
        glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.0f, 0.0f)),
        glm::vec4(0.9f, 0.4f, 0.8f, 1.0f))  // Pink
        .outerRadius(0.5f)
        .innerRadius(0.15f)
        .segments(32)
        .rings(16);

    scene.add<Cylinder>("cylinder",
        glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f)),
        glm::vec4(0.3f, 0.9f, 0.4f, 1.0f))  // Green
        .radius(0.3f)
        .height(1.5f)
        .segments(24)
        .flatShading(true);

    scene.add<Cone>("cone",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 2.5f)),
        glm::vec4(0.9f, 0.7f, 0.2f, 1.0f))  // Orange
        .radius(0.4f)
        .height(1.0f)
        .segments(24)
        .flatShading(true);

    // -------------------------------------------------------------------------
    // CSG: Hollow cube (box - sphere)
    // CSG inputs are created via chain.add<>() (not added to scene)
    // -------------------------------------------------------------------------

    auto& box = chain.add<Box>("box")
        .size(1.2f, 1.2f, 1.2f)
        .flatShading(true);

    auto& sphere = chain.add<Sphere>("sphere")
        .radius(0.85f)
        .segments(24);

    auto& hollowCube = chain.add<Boolean>("hollowCube")
        .inputA("box")
        .inputB("sphere")
        .operation(BooleanOp::Subtract)
        .flatShading(true);

    // Add CSG result to scene
    scene.add(&hollowCube,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
        glm::vec4(0.4f, 0.8f, 1.0f, 1.0f));  // Light blue

    // =========================================================================
    // CAMERA - Required input for 3D rendering
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera")
        .orbitCenter(0.0f, 0.0f, 0.0f)
        .distance(8.0f)
        .elevation(0.3f)
        .azimuth(0.0f)
        .fov(50.0f)
        .nearPlane(0.1f)
        .farPlane(100.0f);

    // =========================================================================
    // RENDER3D - Render scene to texture
    // Set explicit resolution for the render target (1920x1080 for HD output)
    // =========================================================================

    auto& render = chain.add<Render3D>("render3d")
        .input("scene")
        .cameraInput(&camera)
        .shadingMode(ShadingMode::Flat)
        .lightDirection(glm::normalize(glm::vec3(1, 2, 1)))
        .lightColor(glm::vec3(1, 1, 1))
        .ambient(0.2f)
        .clearColor(0.08f, 0.08f, 0.12f)
        .resolution(1920, 1080);  // Render at 1080p

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
