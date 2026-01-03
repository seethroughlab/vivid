// Fog Test - Distance-based Fog Effect
// Demonstrates the Fog post-processing operator

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Ground Plane
    // =========================================================================
    auto& plane = chain.add<Plane>("ground");
    plane.size(50.0f, 50.0f);

    // =========================================================================
    // Spheres at various distances
    // =========================================================================
    auto& sphere1 = chain.add<Sphere>("sphere1");
    sphere1.radius(1.0f);
    sphere1.segments(32);

    auto& sphere2 = chain.add<Sphere>("sphere2");
    sphere2.radius(1.0f);
    sphere2.segments(32);

    auto& sphere3 = chain.add<Sphere>("sphere3");
    sphere3.radius(1.0f);
    sphere3.segments(32);

    auto& sphere4 = chain.add<Sphere>("sphere4");
    sphere4.radius(1.0f);
    sphere4.segments(32);

    // =========================================================================
    // Scene Composition - Objects at increasing distances
    // =========================================================================
    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane
    glm::mat4 groundTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0, -1.5f, 0));
    scene.add(&plane, groundTransform, glm::vec4(0.4f, 0.45f, 0.4f, 1.0f));

    // Spheres at increasing distances (red gradient shows depth)
    glm::mat4 s1 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f));
    glm::mat4 s2 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -15.0f));
    glm::mat4 s3 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -25.0f));
    glm::mat4 s4 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -35.0f));

    scene.add(&sphere1, s1, glm::vec4(1.0f, 0.3f, 0.3f, 1.0f));  // Red
    scene.add(&sphere2, s2, glm::vec4(0.3f, 1.0f, 0.3f, 1.0f));  // Green
    scene.add(&sphere3, s3, glm::vec4(0.3f, 0.3f, 1.0f, 1.0f));  // Blue
    scene.add(&sphere4, s4, glm::vec4(1.0f, 1.0f, 0.3f, 1.0f));  // Yellow

    // =========================================================================
    // Lighting
    // =========================================================================
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(0.5f, -1.0f, 0.3f);
    sun.color(1.0f, 0.95f, 0.9f);
    sun.intensity = 1.5f;

    // =========================================================================
    // Camera - looking down the row of spheres
    // =========================================================================
    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(0, 2, 5);
    camera.target(0, 0, -20);
    camera.fov(60.0f);
    camera.farPlane(100.0f);  // Extended for fog demo

    // =========================================================================
    // Render with depth output for fog
    // =========================================================================
    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sun);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.2f);
    render.setDepthOutput(true);  // Required for fog!
    render.setClearColor(0.5f, 0.55f, 0.6f, 1.0f);  // Match fog color

    // =========================================================================
    // Fog post-processing
    // =========================================================================
    auto& fog = chain.add<Fog>("fog");
    fog.input(&render);
    fog.fogColor[0] = 0.5f;   // Light gray-blue fog
    fog.fogColor[1] = 0.55f;
    fog.fogColor[2] = 0.6f;
    fog.fogStart = 5.0f;      // Fog starts at 5 units
    fog.fogEnd = 40.0f;       // Full fog at 40 units
    fog.fogMode = FogMode::Linear;

    chain.output("fog");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Fog Test - Distance-based Fog Effect" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "4 spheres at distances: 5, 15, 25, 35" << std::endl;
    std::cout << "Fog: start=5, end=40 (linear)" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    // Static scene for testing
}

VIVID_CHAIN(setup, update)
