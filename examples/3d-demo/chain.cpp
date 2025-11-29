// 3D Graphics Demo
// Demonstrates the new 3D rendering features
//
// This example creates:
// - Rotating cube, sphere, and torus primitives
// - Mouse-controlled camera orbit
//
// Controls:
//   Mouse X: Camera orbit horizontal
//   Mouse Y: Camera orbit vertical
//   Scroll: Zoom in/out
//   Click: Reset camera

#include <vivid/vivid.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace vivid;

// Store meshes globally (created in setup, used in update)
static Mesh3D cube;
static Mesh3D sphere;
static Mesh3D torus;
static Camera3D camera;
static Texture output;

void setup(Chain& chain) {
    // Tell the chain to use "out" as the final output
    chain.setOutput("out");
}

void update(Chain& chain, Context& ctx) {
    // Create meshes on first frame
    if (!cube.handle) {
        cube = ctx.createCube();
        sphere = ctx.createSphere(0.4f, 32, 16);
        torus = ctx.createTorus(0.35f, 0.12f);
        output = ctx.createTexture();

        // Initialize camera
        camera.position = glm::vec3(0, 2, 5);
        camera.target = glm::vec3(0, 0, 0);
        camera.fov = 60.0f;
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

    // Create transforms for each mesh
    float t = ctx.time();

    // Cube: rotating on left
    glm::mat4 cubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0, 0));
    cubeTransform = glm::rotate(cubeTransform, t * 0.5f, glm::vec3(0, 1, 0));
    cubeTransform = glm::rotate(cubeTransform, t * 0.3f, glm::vec3(1, 0, 0));

    // Sphere: bobbing in center
    glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f),
        glm::vec3(0, std::sin(t) * 0.5f, 0));

    // Torus: rotating on right
    glm::mat4 torusTransform = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0, 0));
    torusTransform = glm::rotate(torusTransform, t * 0.7f, glm::vec3(1, 0, 0));
    torusTransform = glm::rotate(torusTransform, t * 0.4f, glm::vec3(0, 1, 0));

    // Render all meshes
    std::vector<Mesh3D> meshes = {cube, sphere, torus};
    std::vector<glm::mat4> transforms = {cubeTransform, sphereTransform, torusTransform};

    ctx.render3D(meshes, transforms, camera, output, glm::vec4(0.1f, 0.1f, 0.15f, 1.0f));

    // Set output for display
    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
