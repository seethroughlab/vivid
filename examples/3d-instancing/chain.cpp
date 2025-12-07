/**
 * 3D Instancing Example - Thousands of Spinning Cubes
 *
 * Demonstrates efficient 3D rendering using GPU instancing.
 * All instances are rendered in a single draw call regardless of count.
 *
 * Features:
 * - 1000+ animated cubes in a single draw call
 * - Orbital camera with mouse control
 * - Per-instance colors and animations
 * - Particle-like orbital physics simulation
 */

#include <vivid/vivid.h>
#include <vivid/operators.h>
#include <vivid/mesh.h>
#include <vivid/pbr_material.h>
#include <vivid/ibl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>
#include <cstdlib>
#include <iostream>

using namespace vivid;

// Particle state
struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    float rotationSpeed;
    float rotation;
    float scale;
    glm::vec4 color;
    float metallic;
    float roughness;
};

// Static state
static std::unique_ptr<InstancedRender3D> renderer;
static std::unique_ptr<Output> output;
static std::vector<Particle> particles;
static Mesh cubeMesh;
static std::unique_ptr<PBRMaterial> bronzeMaterial;
static std::unique_ptr<IBLEnvironment> iblEnv;
static bool initialized = false;

// Camera control
static float cameraYaw = 0.0f;
static float cameraPitch = 0.3f;
static float cameraDistance = 15.0f;
static glm::vec2 lastMousePos{0.0f, 0.0f};
static bool isDragging = false;

glm::vec4 hueToRGB(float h) {
    h = fmod(h, 1.0f);
    if (h < 0) h += 1.0f;

    float r, g, b;
    int i = int(h * 6.0f);
    float f = h * 6.0f - i;
    float q = 1.0f - f;

    switch (i % 6) {
        case 0: r = 1; g = f; b = 0; break;
        case 1: r = q; g = 1; b = 0; break;
        case 2: r = 0; g = 1; b = f; break;
        case 3: r = 0; g = q; b = 1; break;
        case 4: r = f; g = 0; b = 1; break;
        case 5: r = 1; g = 0; b = q; break;
        default: r = 1; g = 1; b = 1; break;
    }

    return glm::vec4(r, g, b, 1.0f);
}

void setup(Context& ctx) {
    // Nothing needed here - initialization happens on first update
}

void update(Context& ctx) {
    // Initialize on first frame
    if (!initialized) {
        std::cout << "[3D Instancing] Initializing..." << std::endl;

        // Create cube mesh
        MeshData data = MeshUtils::createCube();
        cubeMesh.create(ctx.device(), data);

        // Create operators
        renderer = std::make_unique<InstancedRender3D>();
        output = std::make_unique<Output>();
        output->setInput(renderer.get());

        renderer->init(ctx);
        output->init(ctx);

        // Load PBR material
        bronzeMaterial = std::make_unique<PBRMaterial>();
        bronzeMaterial->createDefaults(ctx);
        if (bronzeMaterial->loadFromDirectory(ctx, "assets/materials/bronze-bl", "bronze")) {
            std::cout << "[3D Instancing] Loaded bronze material" << std::endl;
        } else {
            std::cout << "[3D Instancing] Warning: Could not load alien-panels material" << std::endl;
        }

        // Load IBL environment
        iblEnv = std::make_unique<IBLEnvironment>();
        if (iblEnv->init(ctx)) {
            if (iblEnv->loadHDR(ctx, "assets/hdris/bryanston_park_sunrise_4k.hdr")) {
                std::cout << "[3D Instancing] Loaded HDR environment for IBL" << std::endl;
            } else {
                std::cout << "[3D Instancing] Warning: Could not load HDR environment" << std::endl;
            }
        }

        // Configure renderer
        renderer->setMesh(&cubeMesh);
        renderer->setMaterial(bronzeMaterial.get());
        renderer->setEnvironment(iblEnv.get());
        renderer->uvScale(2.0f);  // Match pbr-test UV scale
        renderer->iblScale(1.0f);  // IBL intensity
        renderer->backgroundColor(0.08f, 0.08f, 0.1f);  // Match pbr-test
        renderer->ambientColor(0.4f, 0.4f, 0.45f);       // Match pbr-test
        renderer->setLight(InstancedLight(
            glm::vec3(-0.5f, -0.8f, -0.5f),  // direction (similar to pbr-test key light)
            2.5f,                             // Match pbr-test key light intensity
            glm::vec3(1.0f, 0.95f, 0.9f)     // warm white
        ));

        // Create particles in a sphere distribution
        const int numParticles = 50;  // Fewer, larger cubes
        particles.reserve(numParticles);

        for (int i = 0; i < numParticles; i++) {
            Particle p;

            // Distribute in a sphere
            float phi = acosf(2.0f * (float)rand() / RAND_MAX - 1.0f);
            float theta = 2.0f * 3.14159f * (float)rand() / RAND_MAX;
            float r = 3.0f + 5.0f * powf((float)rand() / RAND_MAX, 0.5f);

            p.position = glm::vec3(
                r * sinf(phi) * cosf(theta),
                r * sinf(phi) * sinf(theta),
                r * cosf(phi)
            );

            // Random velocity (orbital tendency)
            glm::vec3 radial = glm::normalize(p.position);
            glm::vec3 up = glm::vec3(0, 1, 0);
            glm::vec3 tangent = glm::cross(radial, up);
            if (glm::length(tangent) < 0.001f) {
                tangent = glm::cross(radial, glm::vec3(1, 0, 0));
            }
            tangent = glm::normalize(tangent);
            p.velocity = tangent * (0.5f + 1.5f * (float)rand() / RAND_MAX);

            p.rotationSpeed = 0.5f + 1.0f * (float)rand() / RAND_MAX;
            p.rotation = 2.0f * 3.14159f * (float)rand() / RAND_MAX;
            p.scale = 1.0f + 1.5f * (float)rand() / RAND_MAX;  // Very large cubes

            // Use white/light gray to let the texture show through
            float brightness = 0.8f + 0.2f * (float)rand() / RAND_MAX;
            p.color = glm::vec4(brightness, brightness, brightness, 1.0f);

            // Material properties from texture, not per-instance
            p.metallic = 0.0f;   // Let texture define metallic
            p.roughness = 0.5f;  // Let texture define roughness

            particles.push_back(p);
        }

        initialized = true;
        std::cout << "[3D Instancing] Ready! " << numParticles << " cubes" << std::endl;
        std::cout << "  Drag mouse to rotate camera" << std::endl;
        std::cout << "  Right-click + drag to zoom" << std::endl;
    }

    float dt = ctx.dt();

    // Mouse camera control
    glm::vec2 mousePos = ctx.mousePosition();

    if (ctx.isMouseDown(0)) {
        if (isDragging) {
            glm::vec2 delta = mousePos - lastMousePos;
            cameraYaw += delta.x * 0.005f;
            cameraPitch += delta.y * 0.003f;
            cameraPitch = glm::clamp(cameraPitch, -1.5f, 1.5f);
        }
        isDragging = true;
    } else {
        isDragging = false;
    }
    lastMousePos = mousePos;

    // Right-click zoom
    if (ctx.isMouseDown(1)) {
        glm::vec2 delta = mousePos - lastMousePos;
        cameraDistance += delta.y * 0.05f;
    }
    cameraDistance = glm::clamp(cameraDistance, 5.0f, 30.0f);

    // Scroll wheel zoom
    glm::vec2 scroll = ctx.scrollDelta();
    if (std::abs(scroll.y) > 0.01f) {
        cameraDistance -= scroll.y * 0.5f;
        cameraDistance = glm::clamp(cameraDistance, 5.0f, 30.0f);
    }

    // Auto-rotate camera slowly
    cameraYaw += dt * 0.1f;

    // Update camera position
    glm::vec3 camPos = glm::vec3(
        cameraDistance * cosf(cameraPitch) * sinf(cameraYaw),
        cameraDistance * sinf(cameraPitch),
        cameraDistance * cosf(cameraPitch) * cosf(cameraYaw)
    );

    renderer->camera().lookAt(camPos, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    renderer->camera().setPerspective(60.0f, ctx.width() / static_cast<float>(ctx.height()), 0.1f, 100.0f);

    // Update particles
    for (auto& p : particles) {
        // Simple orbital motion with gravity toward center
        glm::vec3 toCenter = -p.position;
        float dist = glm::length(toCenter);
        if (dist > 0.1f) {
            glm::vec3 gravity = glm::normalize(toCenter) * 2.0f / (dist + 1.0f);
            p.velocity += gravity * dt;
        }

        // Apply velocity
        p.position += p.velocity * dt;

        // Update rotation
        p.rotation += p.rotationSpeed * dt;

        // Update color based on speed
        float speed = glm::length(p.velocity);
        p.color.a = 0.7f + 0.3f * glm::min(speed * 0.5f, 1.0f);

        // Keep particles roughly in bounds
        if (glm::length(p.position) > 12.0f) {
            p.velocity -= glm::normalize(p.position) * 0.5f * dt;
        }
    }

    // Build instance data
    std::vector<Instance3D> instances;
    instances.reserve(particles.size());

    for (const auto& p : particles) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), p.position);
        model = glm::rotate(model, p.rotation, glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)));
        model = glm::scale(model, glm::vec3(p.scale));

        Instance3D inst(model, p.color, p.metallic, p.roughness);
        instances.push_back(inst);
    }

    // Upload instances and render
    renderer->setInstances(instances);
    renderer->process(ctx);
    output->process(ctx);

    // Register for visualization
    ctx.registerOperator("out", renderer.get());
}

VIVID_CHAIN(setup, update)
