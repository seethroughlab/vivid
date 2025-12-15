// Lighting Test - Point & Spot Lights
// Demonstrates all light types in Vivid 3D

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
    plane.size(10.0f, 10.0f);

    // =========================================================================
    // Test Objects - spheres to show lighting
    // =========================================================================
    auto& sphere1 = chain.add<Sphere>("sphere1");
    sphere1.radius(0.5f);
    sphere1.segments(32);

    auto& sphere2 = chain.add<Sphere>("sphere2");
    sphere2.radius(0.5f);
    sphere2.segments(32);

    auto& sphere3 = chain.add<Sphere>("sphere3");
    sphere3.radius(0.5f);
    sphere3.segments(32);

    // Central cube
    auto& cube = chain.add<Box>("cube");
    cube.size(0.8f, 0.8f, 0.8f);

    // =========================================================================
    // Scene Composition
    // =========================================================================
    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane (light gray to show colored lighting)
    glm::mat4 groundTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.5f, 0));
    scene.add(&plane, groundTransform, glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));

    // Spheres in a triangle around center
    glm::mat4 s1 = glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, 0.0f));
    glm::mat4 s2 = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));
    glm::mat4 s3 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.0f));
    scene.add(&sphere1, s1, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));  // White sphere
    scene.add(&sphere2, s2, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
    scene.add(&sphere3, s3, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));

    // Central cube (white to show light colors)
    scene.add(&cube, glm::mat4(1.0f), glm::vec4(0.95f, 0.95f, 0.95f, 1.0f));

    // =========================================================================
    // Lighting - Multiple types
    // =========================================================================

    // Red point light (left)
    auto& redLight = chain.add<PointLight>("redPoint");
    redLight.position(-2.0f, 2.0f, 2.0f);
    redLight.color(1.0f, 0.2f, 0.1f);  // Red
    redLight.intensity = 8.0f;
    redLight.range = 12.0f;

    // Blue point light (right)
    auto& blueLight = chain.add<PointLight>("bluePoint");
    blueLight.position(2.0f, 2.0f, 2.0f);
    blueLight.color(0.1f, 0.3f, 1.0f);  // Blue
    blueLight.intensity = 8.0f;
    blueLight.range = 12.0f;

    // Green spot light (above, pointing down)
    auto& spotLight = chain.add<SpotLight>("greenSpot");
    spotLight.position(0.0f, 5.0f, 0.0f);
    spotLight.direction(0.0f, -1.0f, 0.0f);  // Pointing down
    spotLight.color(0.2f, 1.0f, 0.3f);  // Green
    spotLight.intensity = 10.0f;
    spotLight.range = 15.0f;
    spotLight.spotAngle = 45.0f;   // 45 degree cone
    spotLight.spotBlend = 0.3f;    // Soft edge

    // Ambient fill (directional) - brighter
    auto& ambientLight = chain.add<DirectionalLight>("ambient");
    ambientLight.direction(0.0f, -1.0f, 0.5f);
    ambientLight.color(1.0f, 1.0f, 1.0f);
    ambientLight.intensity = 0.5f;

    // =========================================================================
    // Camera
    // =========================================================================
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0, 0);
    camera.distance(8.0f);
    camera.elevation(0.6f);
    camera.azimuth(0.5f);
    camera.fov(50.0f);

    // =========================================================================
    // Render
    // =========================================================================
    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&redLight);   // Primary light
    render.addLight(&blueLight);       // Additional lights
    render.addLight(&spotLight);
    render.addLight(&ambientLight);
    render.setShadingMode(ShadingMode::Flat);  // Use Flat for clear lighting
    render.setAmbient(0.1f);
    render.setColor(0.1f, 0.1f, 0.15f, 1.0f);  // Dark blue-gray background

    chain.output("render");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Lighting Test - Point & Spot Lights" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Red point light (left)" << std::endl;
    std::cout << "Blue point light (right)" << std::endl;
    std::cout << "Green spot light (above)" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& camera = chain.get<CameraOperator>("camera");

    // Animate camera orbit
    float time = static_cast<float>(ctx.time());
    camera.azimuth(time * 0.2f);

    // Animate spot light position (circular motion)
    auto& spotLight = chain.get<SpotLight>("greenSpot");
    float spotX = std::sin(time * 0.5f) * 2.0f;
    float spotZ = std::cos(time * 0.5f) * 2.0f;
    spotLight.position(spotX, 4.0f, spotZ);
    // Point toward center
    spotLight.direction(-spotX * 0.3f, -1.0f, -spotZ * 0.3f);
}

VIVID_CHAIN(setup, update)
