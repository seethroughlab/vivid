// DiligentChainTest - Tests Diligent rendering with a Chain-like API pattern
// This shows how render3D calls would work through Diligent

#ifdef VIVID_USE_DILIGENT

#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../../runtime/src/diligent_renderer.h"
#include "../../runtime/src/diligent_pbr.h"

using namespace vivid;

// === SCENE STATE (like Chain globals) ===
static DiligentMeshData groundPlane;
static DiligentMeshData cubeMesh;
static DiligentMeshData sphereMesh;
static bool initialized = false;

// === HELPER: Create vertex ===
Vertex3D makeVertex(glm::vec3 pos, glm::vec3 norm, glm::vec2 uv) {
    Vertex3D v;
    v.position = pos;
    v.normal = norm;
    v.uv = uv;
    v.tangent = glm::vec4(1, 0, 0, 1);
    return v;
}

// === MESH GENERATORS (like ctx.createCube(), etc.) ===
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

void createPlaneMesh(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices, float size) {
    float half = size * 0.5f;
    vertices.push_back(makeVertex({-half, 0,  half}, {0, 1, 0}, {0, 0}));
    vertices.push_back(makeVertex({ half, 0,  half}, {0, 1, 0}, {1, 0}));
    vertices.push_back(makeVertex({ half, 0, -half}, {0, 1, 0}, {1, 1}));
    vertices.push_back(makeVertex({-half, 0, -half}, {0, 1, 0}, {0, 1}));
    indices = {0, 1, 2, 0, 2, 3};
}

void createSphereMesh(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                      float radius, int segments, int rings) {
    for (int ring = 0; ring <= rings; ring++) {
        float phi = glm::pi<float>() * ring / rings;
        float y = std::cos(phi);
        float sinPhi = std::sin(phi);

        for (int seg = 0; seg <= segments; seg++) {
            float theta = 2.0f * glm::pi<float>() * seg / segments;
            float x = sinPhi * std::cos(theta);
            float z = sinPhi * std::sin(theta);

            glm::vec3 pos(x * radius, y * radius, z * radius);
            glm::vec3 norm = glm::normalize(pos);
            glm::vec2 uv(static_cast<float>(seg) / segments, static_cast<float>(ring) / rings);

            vertices.push_back(makeVertex(pos, norm, uv));
        }
    }

    for (int ring = 0; ring < rings; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            uint32_t curr = ring * (segments + 1) + seg;
            uint32_t next = curr + segments + 1;

            indices.push_back(curr);
            indices.push_back(next);
            indices.push_back(curr + 1);

            indices.push_back(curr + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
}

// === SETUP (like Chain::setup) ===
void setup(DiligentPBR& pbr) {
    std::vector<Vertex3D> cubeVerts, planeVerts, sphereVerts;
    std::vector<uint32_t> cubeIdx, planeIdx, sphereIdx;

    createCubeMesh(cubeVerts, cubeIdx);
    createPlaneMesh(planeVerts, planeIdx, 20.0f);
    createSphereMesh(sphereVerts, sphereIdx, 0.5f, 32, 16);

    cubeMesh = pbr.createMesh(cubeVerts, cubeIdx);
    groundPlane = pbr.createMesh(planeVerts, planeIdx);
    sphereMesh = pbr.createMesh(sphereVerts, sphereIdx);

    initialized = true;
    std::cout << "Scene setup complete!" << std::endl;
}

// === UPDATE (like Chain::update) ===
void update(DiligentRenderer& renderer, DiligentPBR& pbr, float time) {
    auto* swapChain = renderer.swapChain();
    auto* rtv = swapChain->GetCurrentBackBufferRTV();
    auto* dsv = swapChain->GetDepthBufferDSV();

    // === CAMERA (like ctx.camera) ===
    float camAngle = time * 0.2f;
    float camDist = 12.0f;
    float camHeight = 8.0f;

    Camera3D camera;
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

    // === LIGHTS (like SceneLighting) ===
    DiligentLightData sun;
    sun.type = 0;  // directional
    sun.direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
    sun.color = glm::vec3(1.0f, 0.95f, 0.9f);
    sun.intensity = 1.5f;
    sun.castShadows = true;

    std::vector<DiligentLightData> lights = { sun };

    // === MATERIALS (like PBRMaterial) ===
    PBRMaterial groundMat;
    groundMat.albedo = glm::vec3(0.3f, 0.3f, 0.35f);
    groundMat.roughness = 0.9f;
    groundMat.metallic = 0.0f;

    PBRMaterial cubeMat;
    cubeMat.albedo = glm::vec3(0.8f, 0.2f, 0.2f);
    cubeMat.roughness = 0.4f;
    cubeMat.metallic = 0.0f;

    PBRMaterial sphereMat;
    sphereMat.albedo = glm::vec3(1.0f, 0.85f, 0.4f);
    sphereMat.roughness = 0.3f;
    sphereMat.metallic = 1.0f;

    // === TRANSFORMS ===
    glm::mat4 groundTransform = glm::mat4(1.0f);

    glm::mat4 cube1 = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 1.0f, 0.0f));
    cube1 = glm::rotate(cube1, time * 0.5f, glm::vec3(0, 1, 0));

    glm::mat4 cube2 = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.5f, 1.0f));
    cube2 = glm::scale(cube2, glm::vec3(0.5f));

    glm::mat4 sphere1 = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.5f, -1.0f));

    // === SHADOW PASS (like ctx.renderShadowMap) ===
    glm::vec3 sceneCenter(0.0f, 0.5f, 0.0f);
    float sceneRadius = 6.0f;

    pbr.beginShadowPass(sun, sceneCenter, sceneRadius);
    pbr.renderToShadowMap(cubeMesh, cube1);
    pbr.renderToShadowMap(cubeMesh, cube2);
    pbr.renderToShadowMap(sphereMesh, sphere1);
    pbr.endShadowPass();

    // === MAIN RENDER PASS (like ctx.render3D with shadow) ===
    // Ground (clears RT)
    pbr.render(groundPlane, camera, groundTransform, groundMat, lights,
               rtv, dsv, true, glm::vec4(0.1f, 0.1f, 0.15f, 1.0f));

    // Objects (no clear)
    pbr.render(cubeMesh, camera, cube1, cubeMat, lights, rtv, dsv, false);
    pbr.render(cubeMesh, camera, cube2, cubeMat, lights, rtv, dsv, false);
    pbr.render(sphereMesh, camera, sphere1, sphereMat, lights, rtv, dsv, false);
}

// === CLEANUP ===
void cleanup(DiligentPBR& pbr) {
    if (initialized) {
        pbr.destroyMesh(groundPlane);
        pbr.destroyMesh(cubeMesh);
        pbr.destroyMesh(sphereMesh);
        initialized = false;
    }
}

int main() {
    std::cout << "=== Diligent Chain Test ===" << std::endl;
    std::cout << "Tests Diligent rendering with Chain-like API pattern" << std::endl;

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    const int WIDTH = 1280;
    const int HEIGHT = 720;

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Diligent Chain Test", nullptr, nullptr);
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

    // Setup (like Chain::setup)
    setup(pbr);

    // Timing
    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastFpsTime = startTime;
    int frameCount = 0;

    // Main loop (like runtime main loop)
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        auto now = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float>(now - startTime).count();

        // Begin frame
        renderer.beginFrame();

        // Update (like Chain::update + process)
        update(renderer, pbr, time);

        // End frame and present
        renderer.endFrame();

        frameCount++;

        // FPS display
        float elapsed = std::chrono::duration<float>(now - lastFpsTime).count();
        if (elapsed >= 1.0f) {
            float fps = frameCount / elapsed;
            char title[64];
            snprintf(title, sizeof(title), "Diligent Chain Test - %.1f FPS", fps);
            glfwSetWindowTitle(window, title);
            frameCount = 0;
            lastFpsTime = now;
        }
    }

    // Cleanup
    cleanup(pbr);
    pbr.shutdown();
    renderer.shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "Diligent Chain Test completed." << std::endl;
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
