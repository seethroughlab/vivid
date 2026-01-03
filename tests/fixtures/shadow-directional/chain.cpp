// Shadow Test - Directional Light Shadows
// Tests shadow mapping with directional light

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
    // Ground Plane (receives shadows)
    // Note: Keep within ±8 units to comfortably fit default shadow frustum
    // =========================================================================
    auto& plane = chain.add<Plane>("ground");
    plane.size(6.0f, 6.0f);  // Smaller to fit within shadow frustum

    // =========================================================================
    // Objects (cast shadows)
    // =========================================================================
    auto& cube = chain.add<Box>("cube");
    cube.size(1.0f, 2.0f, 1.0f);

    auto& sphere = chain.add<Sphere>("sphere");
    sphere.radius(0.8f);
    sphere.segments(32);

    auto& cylinder = chain.add<Cylinder>("cylinder");
    cylinder.radius(0.5f);
    cylinder.height(1.5f);
    cylinder.segments(32);

    // =========================================================================
    // Scene Composition
    // =========================================================================
    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane at Y=0 (white to show shadows clearly)
    // Note: Ground receives shadows but doesn't cast them
    auto groundEntry = scene.add(&plane, nullptr);
    groundEntry.setColor(0.9f, 0.9f, 0.9f, 1.0f);
    groundEntry.setCastShadow(false);   // Ground doesn't cast shadows
    groundEntry.setReceiveShadow(true); // Ground receives shadows

    // Cube (left) - floating above ground for visible shadow
    glm::mat4 cubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.2f, 1.8f, 0.0f));
    auto cubeEntry = scene.add(&cube, nullptr);
    cubeEntry.setTransform(cubeTransform);
    cubeEntry.setColor(0.8f, 0.3f, 0.3f, 1.0f);
    cubeEntry.setCastShadow(true);      // Cube casts shadows
    cubeEntry.setReceiveShadow(true);   // Cube can receive shadows too

    // Sphere (center-front) - floating above ground for visible shadow
    glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 1.2f, 0.8f));
    auto sphereEntry = scene.add(&sphere, nullptr);
    sphereEntry.setTransform(sphereTransform);
    sphereEntry.setColor(0.3f, 0.8f, 0.3f, 1.0f);
    sphereEntry.setCastShadow(false);   // Sphere does NOT cast shadows (demo)
    sphereEntry.setReceiveShadow(true); // But it receives them

    // Cylinder (right) - floating above ground for visible shadow
    glm::mat4 cylTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 1.2f, -0.3f));
    auto cylEntry = scene.add(&cylinder, nullptr);
    cylEntry.setTransform(cylTransform);
    cylEntry.setColor(0.3f, 0.3f, 0.8f, 1.0f);
    cylEntry.setCastShadow(true);       // Cylinder casts shadows
    cylEntry.setReceiveShadow(false);   // But does NOT receive them (demo)

    // =========================================================================
    // Sun Light (casts shadows)
    // =========================================================================
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(0.2f, -1.0f, 0.1f);  // More vertical for full coverage
    sun.color(1.0f, 0.98f, 0.95f);     // Warm sunlight
    sun.intensity = 1.2f;
    sun.castShadow(true);              // Enable shadow casting
    sun.shadowBias(0.015f);            // Bias to prevent shadow acne

    // =========================================================================
    // Camera
    // =========================================================================
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0, 0);
    camera.distance(8.0f);
    camera.elevation(0.6f);
    camera.azimuth(0.4f);
    camera.fov(50.0f);

    // =========================================================================
    // Render with Shadows
    // =========================================================================
    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sun);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.2f);
    render.setShadows(true);  // Enable shadow mapping
    render.setShadowMapResolution(1024);
    render.setClearColor(0.6f, 0.7f, 0.9f, 1.0f);  // Light blue sky

    chain.output("render");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Shadow Test - Directional Light" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Shadow mapping enabled (1024x1024)" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "Shadow toggles demo:" << std::endl;
    std::cout << "  - Ground: receives shadows, doesn't cast" << std::endl;
    std::cout << "  - Red cube: casts and receives shadows" << std::endl;
    std::cout << "  - Green sphere: no shadow (castShadow=false)" << std::endl;
    std::cout << "  - Blue cylinder: casts but doesn't receive" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& sun = chain.get<DirectionalLight>("sun");

    // Animate sun direction - swing dramatically for visible shadow movement
    float time = static_cast<float>(ctx.time());
    float angle = time * 2.0f;  // Faster rotation
    float x = std::sin(angle) * 1.0f;
    float z = std::cos(angle) * 1.0f;
    sun.direction(x, -0.5f, z);  // More horizontal = longer shadows
}

VIVID_CHAIN(setup, update)
