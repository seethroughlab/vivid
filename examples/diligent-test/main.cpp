// DiligentPBR Test - Standalone test for Diligent PBR rendering
// Bypasses the normal Context/Chain system to directly test DiligentRenderer and DiligentPBR

#ifdef VIVID_USE_DILIGENT

#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../../runtime/src/diligent_renderer.h"
#include "../../runtime/src/diligent_pbr.h"

using namespace vivid;

// Helper to create a vertex
Vertex3D makeVertex(glm::vec3 pos, glm::vec3 norm, glm::vec2 uv) {
    Vertex3D v;
    v.position = pos;
    v.normal = norm;
    v.uv = uv;
    v.tangent = glm::vec4(1, 0, 0, 1);
    return v;
}

// Generate a simple cube mesh
void createCubeMesh(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices) {
    // Front face (Z+)
    vertices.push_back(makeVertex({-1, -1,  1}, { 0, 0, 1}, {0, 0}));
    vertices.push_back(makeVertex({ 1, -1,  1}, { 0, 0, 1}, {1, 0}));
    vertices.push_back(makeVertex({ 1,  1,  1}, { 0, 0, 1}, {1, 1}));
    vertices.push_back(makeVertex({-1,  1,  1}, { 0, 0, 1}, {0, 1}));

    // Back face (Z-)
    vertices.push_back(makeVertex({ 1, -1, -1}, { 0, 0,-1}, {0, 0}));
    vertices.push_back(makeVertex({-1, -1, -1}, { 0, 0,-1}, {1, 0}));
    vertices.push_back(makeVertex({-1,  1, -1}, { 0, 0,-1}, {1, 1}));
    vertices.push_back(makeVertex({ 1,  1, -1}, { 0, 0,-1}, {0, 1}));

    // Top face (Y+)
    vertices.push_back(makeVertex({-1,  1,  1}, { 0, 1, 0}, {0, 0}));
    vertices.push_back(makeVertex({ 1,  1,  1}, { 0, 1, 0}, {1, 0}));
    vertices.push_back(makeVertex({ 1,  1, -1}, { 0, 1, 0}, {1, 1}));
    vertices.push_back(makeVertex({-1,  1, -1}, { 0, 1, 0}, {0, 1}));

    // Bottom face (Y-)
    vertices.push_back(makeVertex({-1, -1, -1}, { 0,-1, 0}, {0, 0}));
    vertices.push_back(makeVertex({ 1, -1, -1}, { 0,-1, 0}, {1, 0}));
    vertices.push_back(makeVertex({ 1, -1,  1}, { 0,-1, 0}, {1, 1}));
    vertices.push_back(makeVertex({-1, -1,  1}, { 0,-1, 0}, {0, 1}));

    // Right face (X+)
    vertices.push_back(makeVertex({ 1, -1,  1}, { 1, 0, 0}, {0, 0}));
    vertices.push_back(makeVertex({ 1, -1, -1}, { 1, 0, 0}, {1, 0}));
    vertices.push_back(makeVertex({ 1,  1, -1}, { 1, 0, 0}, {1, 1}));
    vertices.push_back(makeVertex({ 1,  1,  1}, { 1, 0, 0}, {0, 1}));

    // Left face (X-)
    vertices.push_back(makeVertex({-1, -1, -1}, {-1, 0, 0}, {0, 0}));
    vertices.push_back(makeVertex({-1, -1,  1}, {-1, 0, 0}, {1, 0}));
    vertices.push_back(makeVertex({-1,  1,  1}, {-1, 0, 0}, {1, 1}));
    vertices.push_back(makeVertex({-1,  1, -1}, {-1, 0, 0}, {0, 1}));

    // Indices for 6 faces, 2 triangles each
    for (int face = 0; face < 6; face++) {
        uint32_t base = face * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
}

// Generate a plane mesh
void createPlaneMesh(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices, float size) {
    float half = size * 0.5f;

    vertices.push_back(makeVertex({-half, 0,  half}, {0, 1, 0}, {0, 0}));
    vertices.push_back(makeVertex({ half, 0,  half}, {0, 1, 0}, {1, 0}));
    vertices.push_back(makeVertex({ half, 0, -half}, {0, 1, 0}, {1, 1}));
    vertices.push_back(makeVertex({-half, 0, -half}, {0, 1, 0}, {0, 1}));

    indices = {0, 1, 2, 0, 2, 3};
}

int main() {
    std::cout << "=== DiligentPBR Test ===" << std::endl;

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    // No OpenGL context - we'll use Vulkan/Metal via Diligent
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    const int WIDTH = 1280;
    const int HEIGHT = 720;

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "DiligentPBR Test", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return 1;
    }

    // Initialize Diligent renderer
    DiligentRenderer renderer;
    if (!renderer.init(window, WIDTH, HEIGHT)) {
        std::cerr << "Failed to initialize DiligentRenderer" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Initialize DiligentPBR
    DiligentPBR pbr;
    if (!pbr.init(renderer)) {
        std::cerr << "Failed to initialize DiligentPBR" << std::endl;
        renderer.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    std::cout << "DiligentPBR initialized successfully!" << std::endl;

    // Create cube mesh
    std::vector<Vertex3D> cubeVertices;
    std::vector<uint32_t> cubeIndices;
    createCubeMesh(cubeVertices, cubeIndices);
    DiligentMeshData cubeMesh = pbr.createMesh(cubeVertices, cubeIndices);

    // Create plane mesh
    std::vector<Vertex3D> planeVertices;
    std::vector<uint32_t> planeIndices;
    createPlaneMesh(planeVertices, planeIndices, 20.0f);
    DiligentMeshData planeMesh = pbr.createMesh(planeVertices, planeIndices);

    std::cout << "Meshes created!" << std::endl;

    // Main loop
    float time = 0.0f;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        time += 0.016f;  // ~60fps

        // Camera
        Camera3D camera;
        float camAngle = time * 0.3f;
        float camDist = 8.0f;
        float camHeight = 5.0f;
        camera.position = glm::vec3(
            std::cos(camAngle) * camDist,
            camHeight,
            std::sin(camAngle) * camDist
        );
        camera.target = glm::vec3(0, 0, 0);
        camera.up = glm::vec3(0, 1, 0);
        camera.fov = 45.0f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 100.0f;

        // Lights
        std::vector<DiligentLightData> lights;

        // Directional light (sun)
        DiligentLightData sun;
        sun.type = 0;  // directional
        sun.direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
        sun.color = glm::vec3(1.0f, 0.95f, 0.9f);
        sun.intensity = 1.5f;
        sun.castShadows = true;
        lights.push_back(sun);

        // Materials
        PBRMaterial groundMat;
        groundMat.albedo = glm::vec3(0.3f, 0.3f, 0.35f);
        groundMat.roughness = 0.9f;
        groundMat.metallic = 0.0f;

        PBRMaterial cubeMat;
        cubeMat.albedo = glm::vec3(0.8f, 0.2f, 0.2f);
        cubeMat.roughness = 0.4f;
        cubeMat.metallic = 0.0f;

        // Begin frame
        renderer.beginFrame();

        // Get swap chain views
        auto* swapChain = renderer.swapChain();
        auto* rtv = swapChain->GetCurrentBackBufferRTV();
        auto* dsv = swapChain->GetDepthBufferDSV();

        // DEBUG: Skip shadow map pass for now to isolate main rendering
        // glm::vec3 sceneCenter(0, 0.5f, 0);
        // float sceneRadius = 6.0f;
        // pbr.beginShadowPass(sun, sceneCenter, sceneRadius);
        // pbr.renderToShadowMap(cubeMesh, cubeTransform);
        // pbr.endShadowPass();

        // Cube transform (without shadow pass)
        glm::mat4 cubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0));
        cubeTransform = glm::rotate(cubeTransform, time * 0.5f, glm::vec3(0, 1, 0));

        // Render scene to swap chain
        // First render ground (clears)
        glm::mat4 groundTransform = glm::mat4(1.0f);
        pbr.render(planeMesh, camera, groundTransform, groundMat, lights, rtv, dsv, true, glm::vec4(0.1f, 0.1f, 0.15f, 1.0f));

        // Then render cube (no clear)
        pbr.render(cubeMesh, camera, cubeTransform, cubeMat, lights, rtv, dsv, false);

        // End frame and present
        renderer.endFrame();
    }

    // Cleanup
    pbr.destroyMesh(planeMesh);
    pbr.destroyMesh(cubeMesh);
    pbr.shutdown();
    renderer.shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "DiligentPBR test completed." << std::endl;
    return 0;
}

#else

#include <iostream>

int main() {
    std::cerr << "This test requires VIVID_USE_DILIGENT to be enabled." << std::endl;
    std::cerr << "Build with: cmake -DVIVID_USE_DILIGENT=ON .." << std::endl;
    return 1;
}

#endif
