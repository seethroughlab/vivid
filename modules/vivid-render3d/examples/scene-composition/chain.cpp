/**
 * Scene Composition Example
 *
 * Demonstrates: SceneComposer, Render3D, CameraOperator
 *
 * Shows how to compose multiple 3D meshes into a scene
 * with transforms, colors, and coordinated rendering.
 */

#include <vivid/vivid.h>
#include <vivid/render3d/render3d.h>

using namespace vivid;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create scene composer (manages all meshes)
    auto& scene = SceneComposer::create(chain, "scene");

    // Add ground plane
    auto& ground = scene.add<Plane>("ground",
        glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)),  // Large scale
        glm::vec4(0.3f, 0.3f, 0.35f, 1.0f)              // Dark gray
    );
    ground.subdivisions(1, 1);

    // Add central pillar
    auto& pillar = scene.add<Cylinder>("pillar",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
        glm::vec4(0.8f, 0.6f, 0.4f, 1.0f)  // Warm brown
    );
    pillar.radius(0.3f);
    pillar.height(2.0f);
    pillar.segments(16);

    // Add sphere on top of pillar
    auto& orb = scene.add<Sphere>("orb",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.5f, 0.0f)),
        glm::vec4(1.0f, 0.8f, 0.2f, 1.0f)  // Golden
    );
    orb.radius(0.5f);
    orb.segments(24);

    // Add surrounding boxes
    for (int i = 0; i < 6; i++) {
        float angle = i * (3.14159f * 2.0f / 6.0f);
        float x = std::cos(angle) * 3.0f;
        float z = std::sin(angle) * 3.0f;

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.4f, z));
        glm::vec4 color(
            0.4f + 0.3f * std::cos(angle),
            0.5f,
            0.4f + 0.3f * std::sin(angle),
            1.0f
        );

        auto& box = scene.add<Box>("box" + std::to_string(i), transform, color);
        box.size(0.8f);
    }

    // Camera
    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(5.0f, 4.0f, 5.0f);
    camera.target(0.0f, 1.0f, 0.0f);
    camera.fov(45.0f);

    // Directional light (sun)
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(-0.5f, -1.0f, -0.3f);
    sun.color(1.0f, 0.95f, 0.9f);
    sun.intensity = 1.2f;

    // Renderer
    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sun);
    render.setClearColor(0.1f, 0.12f, 0.15f);
    render.setAmbient(0.2f);

    chain.output("render");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& scene = chain.get<SceneComposer>("scene");
    auto& camera = chain.get<CameraOperator>("camera");

    // Orbit camera around scene
    float camDist = 7.0f;
    float camHeight = 4.0f + std::sin(t * 0.3f);
    float camAngle = t * 0.2f;

    camera.position(
        std::cos(camAngle) * camDist,
        camHeight,
        std::sin(camAngle) * camDist
    );

    // Animate the orb (entry index 2)
    float orbY = 2.5f + 0.3f * std::sin(t * 2.0f);
    scene.setEntryTransform(2,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, orbY, 0.0f))
    );

    // Animate surrounding boxes
    for (int i = 0; i < 6; i++) {
        float angle = i * (3.14159f * 2.0f / 6.0f) + t * 0.5f;
        float x = std::cos(angle) * 3.0f;
        float z = std::sin(angle) * 3.0f;
        float y = 0.4f + 0.2f * std::sin(t * 2.0f + i);

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
        transform = glm::rotate(transform, t + i, glm::vec3(0, 1, 0));

        scene.setEntryTransform(3 + i, transform);  // Boxes start at index 3
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
