// 3D Graphics Demo with PBR Materials
// Demonstrates 3D rendering with the PBRMat material operator
//
// This example shows:
// - Defining materials in setup() using chain.add<PBRMat>()
// - Using material presets (gold, copper, rubber)
// - Getting materials in update() with chain.get<PBRMat>().getMaterial()
// - PBR rendering with proper lighting
//
// Controls:
//   Mouse X: Camera orbit horizontal
//   Mouse Y: Camera orbit vertical
//   Click: Reset camera

#include <vivid/vivid.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace vivid;

// Store meshes globally (created in first update frame)
static Mesh3D cube;
static Mesh3D sphere;
static Mesh3D torus;
static Mesh3D ellipticTorus;
static Camera3D camera;
static Texture output;
static SceneLighting lighting;

void setup(Chain& chain) {
    // Define materials using PBRMat operator
    // These are created once in setup and accessed by name in update

    // Gold cube
    chain.add<PBRMat>("gold").gold();

    // Polished copper sphere
    chain.add<PBRMat>("copper").copper();

    // Red rubber torus
    chain.add<PBRMat>("rubber").rubber(0.8f, 0.2f, 0.2f);

    // Silver elliptic torus
    chain.add<PBRMat>("silver").silver();

    // Tell the chain to use "out" as the final output
    chain.setOutput("out");
}

void update(Chain& chain, Context& ctx) {
    // Create meshes and lighting on first frame
    if (!cube.handle) {
        cube = ctx.createCube();
        sphere = ctx.createSphere(0.4f, 32, 16);
        torus = ctx.createTorus(0.35f, 0.12f);
        ellipticTorus = ctx.createEllipticTorus(0.8f, 0.4f, 0.1f);
        output = ctx.createTexture();

        // Initialize camera
        camera.position = glm::vec3(0, 2, 5);
        camera.target = glm::vec3(0, 0, 0);
        camera.fov = 60.0f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 100.0f;

        // Setup lighting for PBR
        lighting.ambientColor = glm::vec3(0.1f);
        lighting.ambientIntensity = 0.3f;

        // Key light (warm, from upper left)
        lighting.addLight(Light::directional(
            glm::vec3(-0.5f, -1.0f, -0.5f),
            glm::vec3(1.0f, 0.95f, 0.9f),
            1.2f
        ));

        // Fill light (cool, from right)
        lighting.addLight(Light::directional(
            glm::vec3(0.8f, -0.3f, 0.5f),
            glm::vec3(0.7f, 0.8f, 1.0f),
            0.4f
        ));
    }

    // Update camera based on mouse
    float orbitX = (ctx.mouseNormX() - 0.5f) * 6.28f;  // Full rotation
    float orbitY = (ctx.mouseNormY() - 0.5f) * 3.14f;  // Half rotation

    // Apply scroll for zoom
    float zoom = 5.0f;  // Base distance

    // Calculate camera position on orbit
    camera.position.x = std::sin(orbitX) * std::cos(orbitY) * zoom;
    camera.position.y = std::sin(orbitY) * zoom + 1.0f;
    camera.position.z = std::cos(orbitX) * std::cos(orbitY) * zoom;

    // Reset on click
    if (ctx.wasMousePressed(0)) {
        camera.position = glm::vec3(0, 2, 5);
    }

    // Get materials from chain (defined in setup)
    auto& goldMat = chain.get<PBRMat>("gold").getMaterial();
    auto& copperMat = chain.get<PBRMat>("copper").getMaterial();
    auto& rubberMat = chain.get<PBRMat>("rubber").getMaterial();
    auto& silverMat = chain.get<PBRMat>("silver").getMaterial();

    // Create transforms for each mesh
    float t = ctx.time();

    // Background color
    glm::vec4 bgColor(0.1f, 0.1f, 0.15f, 1.0f);
    // Use negative alpha after first render to avoid clearing
    glm::vec4 noClear(0.0f, 0.0f, 0.0f, -1.0f);

    // Cube: gold, rotating on left
    glm::mat4 cubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0, 0));
    cubeTransform = glm::rotate(cubeTransform, t * 0.5f, glm::vec3(0, 1, 0));
    cubeTransform = glm::rotate(cubeTransform, t * 0.3f, glm::vec3(1, 0, 0));
    ctx.render3DPBR(cube, camera, cubeTransform, goldMat, lighting, output, bgColor);

    // Sphere: copper, bobbing in center
    glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f),
        glm::vec3(0, std::sin(t) * 0.5f, 0));
    ctx.render3DPBR(sphere, camera, sphereTransform, copperMat, lighting, output, noClear);

    // Torus: red rubber, rotating on right
    glm::mat4 torusTransform = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0, 0));
    torusTransform = glm::rotate(torusTransform, t * 0.7f, glm::vec3(1, 0, 0));
    torusTransform = glm::rotate(torusTransform, t * 0.4f, glm::vec3(0, 1, 0));
    ctx.render3DPBR(torus, camera, torusTransform, rubberMat, lighting, output, noClear);

    // Elliptic torus: silver, rotating above the scene
    glm::mat4 ellipticTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1.5f, 0));
    ellipticTransform = glm::rotate(ellipticTransform, t * 0.3f, glm::vec3(0, 1, 0));
    ellipticTransform = glm::rotate(ellipticTransform, glm::radians(90.0f), glm::vec3(1, 0, 0));
    ctx.render3DPBR(ellipticTorus, camera, ellipticTransform, silverMat, lighting, output, noClear);

    // Set output for display
    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
