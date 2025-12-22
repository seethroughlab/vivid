// Frustum Culling Demo
// Demonstrates how frustum culling reduces rendered instance count
// Watch the debug overlay to see culling stats as the camera orbits

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Cube geometry for instancing
    auto& cubeGeo = chain.add<Box>("cubeGeo");
    cubeGeo.size(0.8f, 0.8f, 0.8f);

    // Sphere geometry for outer ring
    auto& sphereGeo = chain.add<Sphere>("sphereGeo");
    sphereGeo.radius(0.5f);
    sphereGeo.segments(12);

    // Camera with moderate FOV
    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(0, 15, 50);
    camera.target(0, 5, 0);
    camera.fov(45.0f);
    camera.drawDebug(true);  // Show frustum wireframe

    // Directional light
    auto& light = chain.add<DirectionalLight>("sun");
    light.direction(-0.5f, -1.0f, -0.3f);
    light.color(1.0f, 0.95f, 0.9f);
    light.intensity = 1.2f;

    // Instanced cubes - central grid
    auto& cubes = chain.add<InstancedRender3D>("cubes");
    cubes.setMesh(&cubeGeo);
    cubes.setCameraInput(&camera);
    cubes.setLightInput(&light);
    cubes.ambient = 0.2f;
    cubes.setFrustumCulling(true);
    cubes.setClearColor(0.12f, 0.12f, 0.18f, 1.0f);  // Background color

    // Create a 15x5x15 grid = 1125 cubes
    const int gridX = 15, gridY = 5, gridZ = 15;
    const float spacing = 3.0f;

    for (int x = -gridX/2; x <= gridX/2; x++) {
        for (int y = 0; y < gridY; y++) {
            for (int z = -gridZ/2; z <= gridZ/2; z++) {
                Instance3D inst;
                inst.transform = glm::translate(glm::mat4(1.0f),
                    glm::vec3(x * spacing, y * spacing, z * spacing));

                // Color based on position for visual variety
                float r = (float)(x + gridX/2) / gridX;
                float g = (float)y / gridY;
                float b = (float)(z + gridZ/2) / gridZ;
                inst.color = glm::vec4(r * 0.5f + 0.3f, g * 0.5f + 0.3f, b * 0.5f + 0.3f, 1.0f);

                cubes.addInstance(inst);
            }
        }
    }

    // Instanced spheres - scattered ring around the scene
    auto& spheres = chain.add<InstancedRender3D>("spheres");
    spheres.setMesh(&sphereGeo);
    spheres.setCameraInput(&camera);
    spheres.setLightInput(&light);
    spheres.ambient = 0.2f;
    spheres.setFrustumCulling(true);
    spheres.setClearColor(0.0f, 0.0f, 0.0f, 0.0f);  // Transparent for overlay

    for (int i = 0; i < 500; i++) {
        Instance3D inst;
        float angle = (float)i * 0.5f;
        float radius = 35.0f + (i % 20) * 2.0f;
        float height = (float)(i % 10) * 2.0f;

        inst.transform = glm::translate(glm::mat4(1.0f),
            glm::vec3(std::cos(angle) * radius, height, std::sin(angle) * radius));
        inst.transform = glm::scale(inst.transform,
            glm::vec3(0.5f + (i % 5) * 0.3f));
        inst.color = glm::vec4(0.9f, 0.5f, 0.2f, 1.0f);

        spheres.addInstance(inst);
    }

    // Composite cubes and spheres together
    // Both InstancedRender3D operators are TextureOperators that output directly
    auto& final = chain.add<Composite>("final");
    final.inputA("cubes");
    final.inputB("spheres");
    final.mode(BlendMode::Over);

    chain.output("final");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Frustum Culling Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Camera orbits the scene automatically." << std::endl;
    std::cout << "Watch debug overlay for culling stats." << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Orbit camera around the scene
    auto& camera = chain.get<CameraOperator>("camera");
    float angle = t * 0.3f;
    float radius = 55.0f + std::sin(t * 0.5f) * 15.0f;
    float height = 20.0f + std::sin(t * 0.7f) * 10.0f;
    camera.position(std::cos(angle) * radius, height, std::sin(angle) * radius);
    camera.target(0, 5, 0);

    // Get and display culling stats
    auto& cubes = chain.get<InstancedRender3D>("cubes");
    auto& spheres = chain.get<InstancedRender3D>("spheres");

    auto [cubeVis, cubeTotal] = cubes.getCullingStats();
    auto [sphereVis, sphereTotal] = spheres.getCullingStats();

    size_t totalVisible = cubeVis + sphereVis;
    size_t totalInstances = cubeTotal + sphereTotal;
    float cullPercent = totalInstances > 0 ?
        100.0f * (1.0f - (float)totalVisible / (float)totalInstances) : 0.0f;

    // Display as debug values (shows in top-left overlay)
    ctx.debug("Cubes Visible", (float)cubeVis);
    ctx.debug("Cubes Total", (float)cubeTotal);
    ctx.debug("Spheres Visible", (float)sphereVis);
    ctx.debug("Spheres Total", (float)sphereTotal);
    ctx.debug("Culled %", cullPercent);
}

VIVID_CHAIN(setup, update)
