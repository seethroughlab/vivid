/**
 * 3D Instancing Example - Thousands of Spinning Cubes
 *
 * Demonstrates efficient 3D rendering using GPU instancing via ctx.drawMeshInstanced().
 * All instances are rendered in a single draw call regardless of count.
 *
 * Features:
 * - 1000+ animated cubes in a single draw call
 * - Orbital camera with mouse control
 * - Per-instance colors and animations
 */

#include <vivid/vivid.h>
#include <cmath>
#include <vector>
#include <cstdlib>

using namespace vivid;

// Particle state
struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    float rotationSpeed;
    float rotation;
    float scale;
    glm::vec4 color;
};

// Global state
static std::vector<Particle> particles;
static Mesh3D cubeMesh;
static Camera3D camera;
static Texture output;
static bool initialized = false;
static float cameraYaw = 0.0f;
static float cameraPitch = 0.3f;
static float cameraDistance = 15.0f;

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

void setup(Chain& chain) {
    chain.setOutput("out");
}

void update(Chain& chain, Context& ctx) {
    // Initialize on first frame
    if (!initialized) {
        output = ctx.createTexture();
        cubeMesh = ctx.createCube();

        // Setup camera
        camera.fov = 60.0f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 100.0f;

        // Create particles in a sphere distribution
        const int numParticles = 1000;
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
            glm::vec3 tangent = glm::normalize(glm::cross(radial, glm::vec3(0, 1, 0)));
            p.velocity = tangent * (0.5f + 1.5f * (float)rand() / RAND_MAX);

            p.rotationSpeed = 1.0f + 3.0f * (float)rand() / RAND_MAX;
            p.rotation = 2.0f * 3.14159f * (float)rand() / RAND_MAX;
            p.scale = 0.1f + 0.2f * (float)rand() / RAND_MAX;

            // Color based on distance from center
            float hue = r / 10.0f;
            p.color = hueToRGB(hue);

            particles.push_back(p);
        }

        initialized = true;
    }

    float dt = ctx.dt();
    float time = ctx.time();

    // Camera control with mouse
    if (ctx.isMouseDown(0)) {
        cameraYaw += (ctx.mouseNormX() - 0.5f) * 0.1f;
        cameraPitch += (ctx.mouseNormY() - 0.5f) * 0.05f;
        cameraPitch = glm::clamp(cameraPitch, -1.5f, 1.5f);
    }

    // Zoom with scroll or mouse Y when right-clicking
    if (ctx.isMouseDown(1)) {
        cameraDistance -= (ctx.mouseNormY() - 0.5f) * 0.5f;
    }
    cameraDistance = glm::clamp(cameraDistance, 5.0f, 30.0f);

    // Auto-rotate camera slowly
    cameraYaw += dt * 0.1f;

    // Update camera position
    camera.position = glm::vec3(
        cameraDistance * cosf(cameraPitch) * sinf(cameraYaw),
        cameraDistance * sinf(cameraPitch),
        cameraDistance * cosf(cameraPitch) * cosf(cameraYaw)
    );
    camera.target = glm::vec3(0, 0, 0);

    // Update particles
    for (auto& p : particles) {
        // Simple orbital motion
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
        model = glm::rotate(model, p.rotation, glm::vec3(0.5f, 1.0f, 0.3f));
        model = glm::scale(model, glm::vec3(p.scale));

        instances.emplace_back(model, p.color);
    }

    // Render all instances in ONE draw call
    ctx.drawMeshInstanced(cubeMesh, instances, camera, output,
                          glm::vec4(0.02f, 0.02f, 0.05f, 1.0f));
    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
