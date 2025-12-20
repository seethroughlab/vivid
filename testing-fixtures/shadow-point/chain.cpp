// Shadow Test - Point Light Shadows (Cube Map)
// Tests omnidirectional shadow mapping with point light

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Store light marker index for updating
static int g_lightMarkerIndex = -1;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Ground Plane (receives shadows from all directions)
    auto& plane = chain.add<Plane>("ground");
    plane.size(12.0f, 12.0f);

    // Objects arranged around the point light
    auto& cube = chain.add<Box>("cube");
    cube.size(1.0f, 1.5f, 1.0f);

    auto& sphere = chain.add<Sphere>("sphere");
    sphere.radius(0.6f);
    sphere.segments(32);

    auto& cylinder = chain.add<Cylinder>("cylinder");
    cylinder.radius(0.4f);
    cylinder.height(1.2f);
    cylinder.segments(24);

    // Light marker - small bright sphere to show light position
    auto& lightMarker = chain.add<Sphere>("lightMarker");
    lightMarker.radius(0.15f);
    lightMarker.segments(12);

    // Scene Composition
    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane at Y=0
    scene.add(&plane, glm::mat4(1.0f), glm::vec4(0.85f, 0.85f, 0.85f, 1.0f));

    // Objects at various positions around the light
    // Cube (front-left)
    glm::mat4 cubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.75f, 2.0f));
    scene.add(&cube, cubeTransform, glm::vec4(0.8f, 0.3f, 0.3f, 1.0f));

    // Sphere (front-right)
    glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.6f, 1.5f));
    scene.add(&sphere, sphereTransform, glm::vec4(0.3f, 0.8f, 0.3f, 1.0f));

    // Cylinder (back)
    glm::mat4 cylTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.6f, -2.0f));
    scene.add(&cylinder, cylTransform, glm::vec4(0.3f, 0.3f, 0.8f, 1.0f));

    // Light marker - bright yellow, will be updated each frame
    glm::mat4 lightMarkerTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 3.0f, 0.0f));
    g_lightMarkerIndex = static_cast<int>(scene.entries().size());  // Save index before adding
    scene.add(&lightMarker, lightMarkerTransform, glm::vec4(1.0f, 1.0f, 0.2f, 1.0f));

    // Point Light (casts shadows in all directions)
    auto& light = chain.add<PointLight>("pointlight");
    light.position(0.0f, 3.0f, 0.0f);  // Centered, above ground
    light.color(1.0f, 0.95f, 0.9f);     // Warm light
    light.intensity = 2.5f;
    light.range = 15.0f;
    light.castShadow(true);
    light.shadowBias(0.01f);

    // Camera
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0, 0);
    camera.distance(10.0f);
    camera.elevation(0.6f);
    camera.azimuth(0.5f);
    camera.fov(50.0f);

    // Render with Point Light Shadows
    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&light);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.1f);
    render.setShadows(true);
    render.setShadowMapResolution(1024);
    render.setClearColor(0.15f, 0.15f, 0.2f, 1.0f);  // Dark background

    chain.output("render");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Shadow Test - Point Light (Cube Map)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Yellow sphere = light position" << std::endl;
    std::cout << "Shadows should point AWAY from yellow sphere" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& light = chain.get<PointLight>("pointlight");
    auto& scene = chain.get<SceneComposer>("scene");

    // Animate point light position in a circle
    float time = static_cast<float>(ctx.time());
    float radius = 2.0f;
    float height = 3.0f + std::sin(time) * 0.5f;  // Bob up and down
    float x = std::cos(time * 0.5f) * radius;
    float z = std::sin(time * 0.5f) * radius;
    light.position(x, height, z);

    // Update light marker position to match
    if (g_lightMarkerIndex >= 0 && g_lightMarkerIndex < static_cast<int>(scene.entries().size())) {
        scene.entries()[g_lightMarkerIndex].transform =
            glm::translate(glm::mat4(1.0f), glm::vec3(x, height, z));
    }
}

VIVID_CHAIN(setup, update)
