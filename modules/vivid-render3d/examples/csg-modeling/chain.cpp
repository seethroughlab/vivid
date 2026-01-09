// CSG Modeling - Vivid Example
// Demonstrates: Boolean (union, subtract, intersect)
//
// Constructive Solid Geometry for creating complex shapes

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
    // CREATE PRIMITIVES FOR CSG
    // CSG inputs are created via chain.add<>() (not added to scene directly)
    // =========================================================================

    // ----- SUBTRACT DEMO: Hollow Sphere -----
    // Outer sphere with inner sphere subtracted
    auto& outerSphere = chain.add<Sphere>("outerSphere");
    outerSphere.radius(1.0f);
    outerSphere.segments(32);

    auto& innerSphere = chain.add<Sphere>("innerSphere");
    innerSphere.radius(0.85f);
    innerSphere.segments(32);

    auto& hollowSphere = chain.add<Boolean>("hollowSphere");
    hollowSphere.setInputA(&outerSphere);
    hollowSphere.setInputB(&innerSphere);
    hollowSphere.setOperation(BooleanOp::Subtract);

    // ----- INTERSECT DEMO: Rounded Cube -----
    // Box intersected with sphere creates rounded corners
    auto& cube = chain.add<Box>("cube");
    cube.size(1.4f, 1.4f, 1.4f);

    auto& roundingSphere = chain.add<Sphere>("roundingSphere");
    roundingSphere.radius(1.05f);
    roundingSphere.segments(32);

    auto& roundedCube = chain.add<Boolean>("roundedCube");
    roundedCube.setInputA(&cube);
    roundedCube.setInputB(&roundingSphere);
    roundedCube.setOperation(BooleanOp::Intersect);
    roundedCube.flatShading = true;

    // ----- UNION DEMO: Snowman -----
    // Two spheres joined together
    auto& bodyBall = chain.add<Sphere>("bodyBall");
    bodyBall.radius(0.7f);
    bodyBall.segments(24);

    auto& headBall = chain.add<Sphere>("headBall");
    headBall.radius(0.5f);
    headBall.segments(24);

    // Note: Offset one sphere to create snowman shape
    // Since primitives generate at origin, we use the scene transform for positioning
    // For union, we add them separately to scene and rely on visual composition

    // =========================================================================
    // SCENE COMPOSITION
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Add hollow sphere (subtract result) - left side
    scene.add(&hollowSphere,
        glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.0f, 0.0f)),
        glm::vec4(0.4f, 0.7f, 1.0f, 1.0f));  // Light blue

    // Add rounded cube (intersect result) - center
    scene.add(&roundedCube,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
        glm::vec4(1.0f, 0.6f, 0.3f, 1.0f));  // Orange

    // Add body and head separately (visual union) - right side
    scene.add(&bodyBall,
        glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, -0.3f, 0.0f)),
        glm::vec4(0.95f, 0.95f, 0.98f, 1.0f));  // White
    scene.add(&headBall,
        glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.7f, 0.0f)),
        glm::vec4(0.95f, 0.95f, 0.98f, 1.0f));  // White

    // =========================================================================
    // CAMERA
    // =========================================================================

    auto& cam = chain.add<CameraOperator>("camera");
    cam.orbitCenter(0.0f, 0.0f, 0.0f);
    cam.distance(10.0f);
    cam.elevation(0.3f);
    cam.azimuth(0.0f);
    cam.fov(50.0f);

    // =========================================================================
    // LIGHTING
    // =========================================================================

    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(-0.5f, -1.0f, -0.3f);
    sun.intensity = 1.2f;
    sun.color(1.0f, 0.98f, 0.95f);

    // =========================================================================
    // RENDERER
    // =========================================================================

    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&cam);
    render.setLightInput(&sun);
    render.setClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    render.setMetallic(0.2f);
    render.setRoughness(0.6f);
    render.setAmbient(0.25f);

    // Post-process
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("render");
    bloom.threshold = 0.8f;
    bloom.intensity = 0.4f;
    bloom.radius = 8.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& cam = chain.get<CameraOperator>("camera");

    // Mouse controls camera orbit
    float mouseX = ctx.mouseNorm().x;
    float mouseY = ctx.mouseNorm().y;

    cam.distance(8.0f + mouseY * 4.0f);
    cam.azimuth(mouseX * 6.28f + t * 0.2f);
    cam.elevation(0.2f + mouseY * 0.4f);

    // Animate scene objects
    auto& scene = chain.get<SceneComposer>("scene");
    auto& entries = scene.entries();

    // Rotate hollow sphere
    entries[0].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), t * 0.5f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), t * 0.3f, glm::vec3(1, 0, 0));

    // Rotate rounded cube
    entries[1].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), t * 0.4f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), t * 0.25f, glm::vec3(0, 0, 1));

    // Gentle wobble for snowman
    float wobble = std::sin(t * 2.0f) * 0.1f;
    entries[2].transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, -0.3f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), wobble, glm::vec3(0, 0, 1));
    entries[3].transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.7f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), wobble * 1.2f, glm::vec3(0, 0, 1));
}

VIVID_CHAIN(setup, update)
