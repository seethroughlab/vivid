// Multi-Lights Demo - Demonstrates directional, point, and spot lights
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create geometry - a floor plane and some objects
    auto& floor = chain.add<Plane>("floor")
        .size(10.0f, 10.0f)
        .subdivisions(1, 1);

    auto& sphere = chain.add<Sphere>("sphere")
        .radius(0.8f)
        .segments(32);

    auto& box = chain.add<Box>("box")
        .size(1.2f, 1.2f, 1.2f);

    auto& torus = chain.add<Torus>("torus")
        .outerRadius(0.6f)
        .innerRadius(0.2f)
        .segments(32)
        .rings(16);

    // Scene composition with multiple objects
    auto& scene = SceneComposer::create(chain, "scene");

    // Floor (rotated to be horizontal)
    glm::mat4 floorTransform = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0));
    floorTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0, -1.5f, 0)) * floorTransform;
    scene.add(&floor, floorTransform, glm::vec4(0.3f, 0.3f, 0.35f, 1.0f));

    // Sphere on left
    glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0, 0));
    scene.add(&sphere, sphereTransform, glm::vec4(0.8f, 0.2f, 0.2f, 1.0f));

    // Box in center
    glm::mat4 boxTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0));
    scene.add(&box, boxTransform, glm::vec4(0.2f, 0.8f, 0.2f, 1.0f));

    // Torus on right
    glm::mat4 torusTransform = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0, 0));
    scene.add(&torus, torusTransform, glm::vec4(0.2f, 0.2f, 0.8f, 1.0f));

    // Camera
    auto& camera = chain.add<CameraOperator>("camera")
        .orbitCenter(0, 0, 0)
        .distance(8.0f)
        .azimuth(0.3f)
        .elevation(0.4f)
        .fov(50.0f);

    // === Multiple Lights ===

    // 1. Directional light (like the sun) - warm color from above-right
    auto& sun = chain.add<DirectionalLight>("sun")
        .direction(1.0f, 1.5f, 0.5f)
        .color(1.0f, 0.95f, 0.9f)
        .intensity(0.5f);

    // 2. Red point light - orbits around the scene
    auto& redLight = chain.add<PointLight>("redLight")
        .position(3.0f, 1.0f, 0.0f)
        .color(1.0f, 0.2f, 0.1f)
        .intensity(3.0f)
        .range(10.0f);

    // 3. Blue point light - opposite side
    auto& blueLight = chain.add<PointLight>("blueLight")
        .position(-3.0f, 1.0f, 0.0f)
        .color(0.1f, 0.3f, 1.0f)
        .intensity(3.0f)
        .range(10.0f);

    // 4. White spot light - shining down from above
    auto& spotlight = chain.add<SpotLight>("spotlight")
        .position(0.0f, 4.0f, 2.0f)
        .direction(0.0f, -1.0f, -0.3f)
        .color(1.0f, 1.0f, 1.0f)
        .intensity(5.0f)
        .range(12.0f)
        .spotAngle(25.0f)
        .spotBlend(0.3f);

    // Render with PBR shading and multiple lights
    auto& render = chain.add<Render3D>("render")
        .input(&scene)
        .cameraInput(&camera)
        .lightInput(&sun)       // Primary light
        .addLight(&redLight)    // Additional lights
        .addLight(&blueLight)
        .addLight(&spotlight)
        .shadingMode(ShadingMode::PBR)
        .metallic(0.0f)
        .roughness(0.4f)
        .ambient(0.1f)
        .clearColor(0.05f, 0.05f, 0.08f, 1.0f);

    chain.output("render");
}

void update(Context& ctx) {
    float t = ctx.time();

    // Animate the red point light in a circle
    auto& redLight = ctx.chain().get<PointLight>("redLight");
    float rx = 3.0f * std::cos(t * 0.8f);
    float rz = 3.0f * std::sin(t * 0.8f);
    redLight.position(rx, 1.0f + 0.5f * std::sin(t * 1.5f), rz);

    // Animate the blue point light in opposite circle
    auto& blueLight = ctx.chain().get<PointLight>("blueLight");
    float bx = 3.0f * std::cos(t * 0.8f + 3.14159f);
    float bz = 3.0f * std::sin(t * 0.8f + 3.14159f);
    blueLight.position(bx, 1.0f + 0.5f * std::cos(t * 1.5f), bz);

    // Animate spotlight to sweep back and forth
    auto& spotlight = ctx.chain().get<SpotLight>("spotlight");
    float spotX = 2.0f * std::sin(t * 0.5f);
    spotlight.position(spotX, 4.0f, 2.0f);
    spotlight.direction(-spotX * 0.3f, -1.0f, -0.3f);

    // Slowly orbit camera
    auto& camera = ctx.chain().get<CameraOperator>("camera");
    camera.azimuth(t * 0.1f);
}

VIVID_CHAIN(setup, update)
