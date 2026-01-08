// Instancing - Vivid Example
// Demonstrates: InstancedRender3D, Box, Sphere
//
// GPU instancing for rendering thousands of objects efficiently

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Instance data
static std::vector<Instance3D> g_instances;
static const int NUM_INSTANCES = 500;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- MESH -----
    // Box mesh that will be instanced
    auto& box = chain.add<Box>("box");
    box.size(1.0f, 1.0f, 1.0f);

    // ----- CAMERA -----
    auto& cam = chain.add<CameraOperator>("camera");
    cam.position(0.0f, 5.0f, 20.0f);
    cam.target(0.0f, 0.0f, 0.0f);
    cam.fov(60.0f);

    // ----- LIGHTS -----
    // Note: InstancedRender3D supports single light - combine sun + fill contribution
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(-0.3f, -1.0f, -0.2f);
    sun.intensity = 1.4f;
    sun.color(1.0f, 0.97f, 0.95f);

    // ----- INSTANCED RENDERER -----
    auto& instanced = chain.add<InstancedRender3D>("instanced");
    instanced.setMesh(&box);
    instanced.setCameraInput(&cam);
    instanced.setLightInput(&sun);
    instanced.setClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    instanced.metallic = 0.2f;
    instanced.roughness = 0.6f;
    instanced.ambient = 0.2f;

    // Initialize instances in a grid pattern
    g_instances.resize(NUM_INSTANCES);
    int gridSize = static_cast<int>(std::ceil(std::cbrt(NUM_INSTANCES)));
    float spacing = 2.5f;
    float offset = gridSize * spacing * 0.5f;

    for (int i = 0; i < NUM_INSTANCES; i++) {
        int x = i % gridSize;
        int y = (i / gridSize) % gridSize;
        int z = i / (gridSize * gridSize);

        float px = x * spacing - offset;
        float py = y * spacing - offset;
        float pz = z * spacing - offset;

        Instance3D& inst = g_instances[i];
        inst.transform = glm::translate(glm::mat4(1.0f), glm::vec3(px, py, pz));

        // Colorful rainbow based on position
        float hue = static_cast<float>(i) / NUM_INSTANCES;
        inst.color = glm::vec4(
            0.5f + 0.5f * std::sin(hue * 6.28f),
            0.5f + 0.5f * std::sin(hue * 6.28f + 2.09f),
            0.5f + 0.5f * std::sin(hue * 6.28f + 4.19f),
            1.0f
        );

        // Vary material properties
        inst.metallic = std::sin(hue * 3.14f) * 0.5f + 0.5f;
        inst.roughness = std::cos(hue * 3.14f) * 0.3f + 0.5f;
    }

    // Post-process
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("instanced");
    bloom.threshold = 0.7f;
    bloom.intensity = 0.6f;
    bloom.radius = 12.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& cam = chain.get<CameraOperator>("camera");
    auto& instanced = chain.get<InstancedRender3D>("instanced");

    // Mouse controls camera
    float mouseX = ctx.mouseNorm().x;
    float mouseY = ctx.mouseNorm().y;

    // Orbit camera around scene
    float camRadius = 25.0f + mouseY * 10.0f;
    float camAngle = t * 0.2f + mouseX * 3.14f;
    cam.position(
        std::cos(camAngle) * camRadius,
        8.0f + mouseY * 5.0f,
        std::sin(camAngle) * camRadius
    );
    cam.target(0.0f, 0.0f, 0.0f);

    // Animate instances
    int gridSize = static_cast<int>(std::ceil(std::cbrt(NUM_INSTANCES)));
    float spacing = 2.5f;
    float offset = gridSize * spacing * 0.5f;

    for (int i = 0; i < NUM_INSTANCES; i++) {
        int x = i % gridSize;
        int y = (i / gridSize) % gridSize;
        int z = i / (gridSize * gridSize);

        float px = x * spacing - offset;
        float py = y * spacing - offset;
        float pz = z * spacing - offset;

        // Add wave motion
        float wave = std::sin(t * 2.0f + (x + z) * 0.3f) * 1.0f;
        py += wave;

        // Build transform with rotation
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(px, py, pz));
        
        float rotY = t * 0.5f + i * 0.1f;
        float rotX = t * 0.3f + i * 0.05f;
        transform = glm::rotate(transform, rotY, glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::rotate(transform, rotX, glm::vec3(1.0f, 0.0f, 0.0f));

        // Scale slightly
        float scale = 0.4f + std::sin(t + i * 0.1f) * 0.1f;
        transform = glm::scale(transform, glm::vec3(scale));

        g_instances[i].transform = transform;

        // Animate colors
        float hue = std::fmod(static_cast<float>(i) / NUM_INSTANCES + t * 0.1f, 1.0f);
        g_instances[i].color = glm::vec4(
            0.5f + 0.5f * std::sin(hue * 6.28f),
            0.5f + 0.5f * std::sin(hue * 6.28f + 2.09f),
            0.5f + 0.5f * std::sin(hue * 6.28f + 4.19f),
            1.0f
        );
    }

    // Update instances
    instanced.setInstances(g_instances);
}

VIVID_CHAIN(setup, update)
