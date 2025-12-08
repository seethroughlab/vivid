// Instancing Demo - GPU-instanced rendering of thousands of objects
// Fly through an asteroid field with PBR textured materials and procedural stars
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <random>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Constants
static constexpr int NUM_ASTEROIDS = 3000;
static constexpr float TUNNEL_LENGTH = 200.0f;
static constexpr float TUNNEL_RADIUS = 8.0f;
static constexpr float CAMERA_SPEED = 8.0f;

// Per-asteroid state
struct AsteroidState {
    glm::vec3 basePosition;  // Position relative to camera Z
    glm::vec3 rotationAxis;
    float rotationSpeed;
    float scale;
    glm::vec4 color;
};

static std::vector<AsteroidState> asteroids;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // === Procedural Star Background ===
    // Use inverted Worley noise for point-like stars
    auto& starNoise = chain.add<Noise>("starNoise")
        .type(NoiseType::Worley)
        .scale(15.0f)      // Many small cells
        .octaves(1)        // No fractal - clean points
        .speed(0.0f);      // Static stars

    // High contrast to create sparse bright points
    auto& stars = chain.add<Brightness>("stars")
        .input(&starNoise)
        .brightness(-0.85f)   // Push most values to black
        .contrast(8.0f)       // Extreme contrast for sharp stars
        .gamma(0.3f);         // Boost the remaining bright points

    // === Asteroid Geometry ===
    // Create asteroid mesh (higher poly for textures)
    auto& asteroid = chain.add<Sphere>("asteroid")
        .radius(0.15f)
        .segments(16)
        .computeTangents();  // Required for normal mapping

    // PBR rock material
    auto& rockMaterial = chain.add<TexturedMaterial>("rockMaterial")
        .baseColor("assets/materials/roughrockface2-bl/roughrockface2_Base_Color.png")
        .normal("assets/materials/roughrockface2-bl/roughrockface2_Normal.png")
        .metallic("assets/materials/roughrockface2-bl/roughrockface2_Metallic.png")
        .roughness("assets/materials/roughrockface2-bl/roughrockface2_Roughness.png")
        .ao("assets/materials/roughrockface2-bl/roughrockface2_Ambient_Occlusion.png");

    // Camera - will be positioned manually in update()
    auto& camera = chain.add<CameraOperator>("camera")
        .fov(70.0f)  // Wider FOV for immersion
        .farPlane(300.0f);  // Extended for tunnel depth

    // Lighting - from behind/above for dramatic effect
    auto& sun = chain.add<DirectionalLight>("sun")
        .direction(0.2f, 0.5f, 1.0f)  // Light from behind
        .color(1.0f, 0.95f, 0.9f)
        .intensity(1.5f);

    // Add a subtle fill light from the front
    auto& fillLight = chain.add<DirectionalLight>("fill")
        .direction(0.0f, 0.3f, -1.0f)
        .color(0.4f, 0.5f, 0.7f)
        .intensity(0.5f);

    // Create instanced renderer with textured material
    // Use transparent clear so stars show through
    auto& instanced = chain.add<InstancedRender3D>("asteroids")
        .mesh(&asteroid)
        .material(&rockMaterial)
        .cameraInput(&camera)
        .lightInput(&sun)
        .addLight(&fillLight)
        .ambient(0.15f)
        .clearColor(0.0f, 0.0f, 0.0f, 0.0f);  // Transparent for compositing

    // Composite asteroids over star background
    auto& final = chain.add<Composite>("final")
        .inputA(&stars)      // Background: stars
        .inputB(&instanced)  // Foreground: asteroids
        .mode(BlendMode::Over);

    // Reserve capacity for asteroids
    instanced.reserve(NUM_ASTEROIDS);

    // Initialize asteroid states - distributed in a tunnel around the flight path
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> distAngle(0.0f, 6.28318f);
    std::uniform_real_distribution<float> distAxis(-1.0f, 1.0f);

    asteroids.resize(NUM_ASTEROIDS);
    for (int i = 0; i < NUM_ASTEROIDS; i++) {
        AsteroidState& a = asteroids[i];

        // Distribute along the tunnel length (will wrap around)
        float z = dist01(rng) * TUNNEL_LENGTH;

        // Distribute in a hollow cylinder - more asteroids near the edges
        float angle = distAngle(rng);
        float radiusBias = dist01(rng);
        // Bias towards outer radius for tunnel effect
        float radius = TUNNEL_RADIUS * (0.3f + 0.7f * std::sqrt(radiusBias));

        // Some asteroids closer to center for near-misses
        if (dist01(rng) < 0.15f) {
            radius = dist01(rng) * TUNNEL_RADIUS * 0.4f;
        }

        a.basePosition = glm::vec3(
            std::cos(angle) * radius,
            std::sin(angle) * radius,
            z
        );

        // Random rotation
        a.rotationAxis = glm::normalize(glm::vec3(distAxis(rng), distAxis(rng), distAxis(rng)));
        a.rotationSpeed = 0.5f + dist01(rng) * 2.0f;

        // Random scale - mix of sizes
        float sizeRoll = dist01(rng);
        if (sizeRoll < 0.7f) {
            a.scale = 0.3f + dist01(rng) * 0.8f;  // Small rocks
        } else if (sizeRoll < 0.95f) {
            a.scale = 1.0f + dist01(rng) * 1.5f;  // Medium rocks
        } else {
            a.scale = 2.5f + dist01(rng) * 2.0f;  // Large boulders
        }

        // Slight color/brightness variation
        float brightness = 0.7f + dist01(rng) * 0.5f;
        a.color = glm::vec4(brightness, brightness, brightness, 1.0f);
    }

    chain.output("final");
}

void update(Context& ctx) {
    float t = ctx.time();

    // Camera flies forward through the tunnel
    float cameraZ = t * CAMERA_SPEED;

    // Gentle side-to-side and up-down sway
    float swayX = std::sin(t * 0.7f) * 0.8f;
    float swayY = std::sin(t * 0.5f) * 0.5f;

    glm::vec3 cameraPos(swayX, swayY, cameraZ);
    glm::vec3 targetPos(swayX * 0.5f, swayY * 0.3f, cameraZ + 10.0f);

    auto& camera = ctx.chain().get<CameraOperator>("camera");
    camera.position(cameraPos.x, cameraPos.y, cameraPos.z);
    camera.target(targetPos.x, targetPos.y, targetPos.z);

    // Update asteroid instances
    auto& instanced = ctx.chain().get<InstancedRender3D>("asteroids");
    instanced.clearInstances();

    for (int i = 0; i < NUM_ASTEROIDS; i++) {
        AsteroidState& a = asteroids[i];

        // Wrap asteroid position relative to camera
        float relZ = a.basePosition.z - std::fmod(cameraZ, TUNNEL_LENGTH);
        if (relZ < -10.0f) relZ += TUNNEL_LENGTH;
        if (relZ > TUNNEL_LENGTH - 10.0f) relZ -= TUNNEL_LENGTH;

        glm::vec3 pos(a.basePosition.x, a.basePosition.y, cameraZ + relZ);

        // Build transform matrix
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos);
        transform = glm::rotate(transform, t * a.rotationSpeed, a.rotationAxis);
        transform = glm::scale(transform, glm::vec3(a.scale));

        // Add instance
        Instance3D inst;
        inst.transform = transform;
        inst.color = a.color;
        inst.metallic = 0.2f;
        inst.roughness = 0.8f;
        instanced.addInstance(inst);
    }
}

VIVID_CHAIN(setup, update)
