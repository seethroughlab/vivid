// Depth of Field Showcase - Vivid
// Demonstrates real depth-based DOF using Render3D's depth output
// Objects at varying depths blur based on focus distance
//
// Controls:
//   LEFT/RIGHT: Adjust focus distance
//   UP/DOWN: Adjust blur strength
//   D: Toggle depth debug view
//   TAB: Open parameter controls

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <iostream>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// DOF parameters
static float g_focusDistance = 0.5f;  // 0=near, 1=far
static float g_blurStrength = 0.6f;
static bool g_showDepth = false;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Geometry
    // =========================================================================

    auto& sphere = chain.add<Sphere>("sphere");
    sphere.radius(0.6f);
    sphere.segments(24);

    auto& box = chain.add<Box>("box");
    box.size(0.9f, 0.9f, 0.9f);

    auto& torus = chain.add<Torus>("torus");
    torus.outerRadius(0.5f);
    torus.innerRadius(0.2f);
    torus.segments(24);
    torus.rings(16);

    // =========================================================================
    // Scene with objects at varying depths - spread across large depth range
    // Color coded: NEAR=red, MID=green, FAR=blue
    // Camera at Z=-8, objects from Z=-5 to Z=50
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // NEAR objects (red/orange) - very close to camera (Z=-5 to -3)
    scene.add(&sphere,
        glm::translate(glm::mat4(1.0f), glm::vec3(-1.2f, 0, -5.0f)),
        glm::vec4(1.0f, 0.3f, 0.2f, 1.0f));
    scene.add(&box,
        glm::translate(glm::mat4(1.0f), glm::vec3(1.2f, 0.3f, -4.0f)),
        glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
    scene.add(&torus,
        glm::translate(glm::mat4(1.0f), glm::vec3(0, 1.2f, -4.5f)),
        glm::vec4(1.0f, 0.4f, 0.3f, 1.0f));

    // MID objects (green) - middle distance (Z=5 to 15)
    scene.add(&sphere,
        glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 8.0f)),
        glm::vec4(0.3f, 0.9f, 0.4f, 1.0f));
    scene.add(&box,
        glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, -0.3f, 10.0f)),
        glm::vec4(0.4f, 0.8f, 0.3f, 1.0f));
    scene.add(&torus,
        glm::rotate(
            glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.5f, 12.0f)),
            0.5f, glm::vec3(1, 0, 0)),
        glm::vec4(0.2f, 1.0f, 0.5f, 1.0f));

    // FAR objects (blue/purple) - very far from camera (Z=30 to 50)
    scene.add(&sphere,
        glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 1.0f, 35.0f)),
            glm::vec3(2.0f)),
        glm::vec4(0.3f, 0.4f, 1.0f, 1.0f));
    scene.add(&sphere,
        glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -0.5f, 40.0f)),
            glm::vec3(2.5f)),
        glm::vec4(0.5f, 0.3f, 1.0f, 1.0f));
    scene.add(&box,
        glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.5f, 50.0f)),
            glm::vec3(3.0f)),
        glm::vec4(0.4f, 0.5f, 0.9f, 1.0f));
    scene.add(&torus,
        glm::scale(
            glm::rotate(
                glm::translate(glm::mat4(1.0f), glm::vec3(-5.0f, -0.5f, 45.0f)),
                1.0f, glm::vec3(0, 1, 0)),
            glm::vec3(2.0f)),
        glm::vec4(0.6f, 0.3f, 1.0f, 1.0f));
    scene.add(&torus,
        glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 1.5f, 48.0f)),
            glm::vec3(2.5f)),
        glm::vec4(0.3f, 0.6f, 1.0f, 1.0f));

    // Ground plane - extended for depth
    auto& plane = chain.add<Plane>("plane");
    plane.size(30.0f, 80.0f);
    plane.subdivisions(1, 1);

    scene.add(&plane,
        glm::rotate(
            glm::translate(glm::mat4(1.0f), glm::vec3(0, -1.5f, 20.0f)),
            -1.57f, glm::vec3(1, 0, 0)),
        glm::vec4(0.12f, 0.12f, 0.15f, 1.0f));

    // =========================================================================
    // Camera and Lighting
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(0, 2.0f, -10.0f);
    camera.target(0, 0, 20.0f);
    camera.fov(45.0f);
    camera.nearPlane(1.0f);
    camera.farPlane(70.0f);  // Match scene depth range

    auto& keyLight = chain.add<DirectionalLight>("keyLight");
    keyLight.direction(1.0f, 2.0f, 0.5f);
    keyLight.color(1.0f, 0.95f, 0.9f);  // Warm white
    keyLight.intensity = 1.8f;

    auto& fillLight = chain.add<DirectionalLight>("fillLight");
    fillLight.direction(-1.0f, 0.5f, -0.5f);
    fillLight.color(0.4f, 0.5f, 0.9f);  // Cool blue
    fillLight.intensity = 0.5f;

    // =========================================================================
    // 3D Render with depth output enabled
    // =========================================================================

    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&keyLight);
    render.addLight(&fillLight);
    render.setShadingMode(ShadingMode::PBR);
    render.setMetallic(0.15f);
    render.setRoughness(0.5f);
    render.setColor(0.03f, 0.03f, 0.06f, 1.0f);  // Dark background
    render.setDepthOutput(true);  // Enable depth output for DOF

    // =========================================================================
    // Depth of Field post-processing
    // =========================================================================

    auto& dof = chain.add<DepthOfField>("dof");
    dof.input(&render);
    dof.focusDistance(g_focusDistance);
    dof.focusRange(0.05f);
    dof.blurStrength(g_blurStrength);

    // =========================================================================
    // Final post-processing
    // =========================================================================

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input(&dof);
    bloom.threshold = 0.8f;
    bloom.intensity = 0.3f;
    bloom.radius = 6.0f;

    auto& vignette = chain.add<CRTEffect>("vignette");
    vignette.input(&bloom);
    vignette.curvature = 0.0f;
    vignette.vignette = 0.4f;
    vignette.scanlines = 0.0f;
    vignette.bloom = 0.0f;
    vignette.chromatic = 0.0f;

    chain.output("vignette");

    // =========================================================================
    // Info
    // =========================================================================

    std::cout << "\n========================================" << std::endl;
    std::cout << "Depth of Field Showcase" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Real depth-based DOF using depth buffer" << std::endl;
    std::cout << "Objects: NEAR (red), MID (green), FAR (blue)" << std::endl;
    std::cout << "\nControls:" << std::endl;
    std::cout << "  LEFT/RIGHT: Focus distance" << std::endl;
    std::cout << "  UP/DOWN: Blur strength" << std::endl;
    std::cout << "  D: Toggle depth debug view" << std::endl;
    std::cout << "  TAB: Parameters" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float dt = static_cast<float>(ctx.dt());

    auto& dof = chain.get<DepthOfField>("dof");

    // =========================================================================
    // Input
    // =========================================================================

    // Toggle depth debug view
    if (ctx.key(GLFW_KEY_D).pressed) {
        g_showDepth = !g_showDepth;
        std::cout << "[DOF] Depth view: " << (g_showDepth ? "ON (green = in focus)" : "OFF") << std::endl;
    }

    // Adjust focus distance
    static float lastFocusDistance = g_focusDistance;
    if (ctx.key(GLFW_KEY_RIGHT).held) {
        g_focusDistance = std::min(g_focusDistance + dt * 0.3f, 1.0f);
    }
    if (ctx.key(GLFW_KEY_LEFT).held) {
        g_focusDistance = std::max(g_focusDistance - dt * 0.3f, 0.0f);
    }
    // Print focus distance when it changes significantly
    if (std::abs(g_focusDistance - lastFocusDistance) > 0.02f) {
        std::cout << "[DOF] Focus: " << g_focusDistance << std::endl;
        lastFocusDistance = g_focusDistance;
    }

    // Adjust blur strength
    if (ctx.key(GLFW_KEY_UP).held) {
        g_blurStrength = std::min(g_blurStrength + dt * 0.3f, 1.0f);
    }
    if (ctx.key(GLFW_KEY_DOWN).held) {
        g_blurStrength = std::max(g_blurStrength - dt * 0.3f, 0.0f);
    }

    // =========================================================================
    // Update DOF parameters
    // =========================================================================

    dof.focusDistance(g_focusDistance);
    dof.blurStrength(g_blurStrength);
    dof.showDepth(g_showDepth);
}

VIVID_CHAIN(setup, update)
