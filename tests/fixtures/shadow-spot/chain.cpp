// Shadow Test - Spot Light Shadows
// Tests shadow mapping with spot light (perspective projection)

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Ground Plane (receives shadows)
    auto& plane = chain.add<Plane>("ground");
    plane.size(10.0f, 10.0f);

    // Objects (cast shadows)
    auto& cube = chain.add<Box>("cube");
    cube.size(1.0f, 1.5f, 1.0f);

    auto& sphere = chain.add<Sphere>("sphere");
    sphere.radius(0.6f);
    sphere.segments(32);

    // Scene Composition
    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane at Y=0
    scene.add(&plane, glm::mat4(1.0f), glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));

    // Cube
    glm::mat4 cubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.75f, 0.0f));
    scene.add(&cube, cubeTransform, glm::vec4(0.8f, 0.3f, 0.3f, 1.0f));

    // Sphere
    glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.6f, 0.0f));
    scene.add(&sphere, sphereTransform, glm::vec4(0.3f, 0.8f, 0.3f, 1.0f));

    // Spot Light (casts shadows)
    auto& spot = chain.add<SpotLight>("spotlight");
    spot.position(0.0f, 5.0f, 3.0f);  // Above and in front
    spot.direction(0.0f, -1.0f, -0.5f);  // Pointing down and forward
    spot.color(1.0f, 0.95f, 0.9f);
    spot.intensity = 2.0f;
    spot.spotAngle = 35.0f;  // Cone angle
    spot.range = 15.0f;
    spot.castShadow(true);
    spot.shadowBias(0.005f);

    // Camera
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0, 0);
    camera.distance(8.0f);
    camera.elevation(0.5f);
    camera.azimuth(0.3f);
    camera.fov(50.0f);

    // Render with Shadows
    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&spot);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.15f);
    render.setShadows(true);
    render.setShadowMapResolution(1024);
    render.setClearColor(0.2f, 0.2f, 0.3f, 1.0f);  // Dark background

    chain.output("render");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Shadow Test - Spot Light" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Spot light with perspective shadow map" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& spot = chain.get<SpotLight>("spotlight");

    // Swing the spotlight side to side
    float time = static_cast<float>(ctx.time());
    float swingX = std::sin(time * 1.2f) * 2.5f;

    // Position swings side to side, stays at same height/depth
    spot.position(swingX, 5.0f, 3.0f);

    // Direction adjusted to always point toward center of scene
    float dirX = -swingX * 0.15f;  // Slight horizontal correction
    spot.direction(dirX, -1.0f, -0.5f);
}

VIVID_CHAIN(setup, update)
