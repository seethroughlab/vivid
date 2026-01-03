// Debug Visualization Test
// Tests wireframe debug visualization for lights and camera

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Ground plane
    auto& plane = chain.add<Plane>("ground");
    plane.size(20.0f, 20.0f);

    // Some objects to cast shadows and show lighting
    auto& cube = chain.add<Box>("cube");
    cube.size(1.0f, 1.0f, 1.0f);

    auto& sphere = chain.add<Sphere>("sphere");
    sphere.radius(0.5f);

    // Scene composition
    auto& scene = SceneComposer::create(chain, "scene");
    scene.add(&plane, glm::mat4(1.0f), glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
    scene.add(&cube,
        glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.5f, 0.0f)),
        glm::vec4(0.8f, 0.3f, 0.3f, 1.0f));
    scene.add(&sphere,
        glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.5f, 0.0f)),
        glm::vec4(0.3f, 0.8f, 0.3f, 1.0f));

    // Directional light - shows yellow arrow
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(1.0f, 2.0f, 1.0f);
    sun.color(1.0f, 0.95f, 0.9f);
    sun.intensity = 0.5f;
    sun.drawDebug(true);

    // Point light - shows orange sphere at position
    auto& pointLight = chain.add<PointLight>("point");
    pointLight.position(0.0f, 3.0f, 3.0f);
    pointLight.color(1.0f, 0.6f, 0.3f);
    pointLight.intensity = 2.0f;
    pointLight.range = 8.0f;
    pointLight.drawDebug(true);

    // Spot light - shows green cone
    auto& spotLight = chain.add<SpotLight>("spot");
    spotLight.position(4.0f, 5.0f, 0.0f);
    spotLight.direction(-0.5f, -1.0f, 0.0f);
    spotLight.color(0.3f, 1.0f, 0.6f);
    spotLight.intensity = 3.0f;
    spotLight.range = 10.0f;
    spotLight.spotAngle = 30.0f;
    spotLight.drawDebug(true);

    // Camera - orbit around scene
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 1, 0);
    camera.distance(15.0f);
    camera.elevation(0.5f);
    camera.azimuth(0.8f);
    camera.fov(50.0f);
    // Note: Can't show camera's own frustum since we're looking through it
    // But you can add a second "debug camera" if needed

    // Render
    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sun);
    render.addLight(&pointLight);
    render.addLight(&spotLight);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.2f);
    render.setClearColor(0.15f, 0.15f, 0.2f, 1.0f);

    chain.output("render");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Debug Visualization Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Yellow arrow = Directional light direction" << std::endl;
    std::cout << "Orange sphere = Point light range" << std::endl;
    std::cout << "Green cone = Spot light cone/range" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    // Animate point light position
    auto& chain = ctx.chain();
    auto& pointLight = chain.get<PointLight>("point");

    float time = static_cast<float>(ctx.time());
    float x = sinf(time * 0.5f) * 3.0f;
    float z = cosf(time * 0.5f) * 3.0f;
    pointLight.position(x, 3.0f, z);
}

VIVID_CHAIN(setup, update)
