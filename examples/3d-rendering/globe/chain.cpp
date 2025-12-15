// Globe - Vivid 3D Example
// A rotating Earth with PBR lighting
//
// Controls:
//   SPACE: Toggle auto-rotation
//   TAB: Open parameter controls

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

static bool g_autoRotate = true;
static float g_rotation = 0.0f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Earth Sphere
    // =========================================================================

    // Create a high-detail sphere for the globe
    auto& sphere = chain.add<Sphere>("earth");
    sphere.radius(1.0f);
    sphere.segments(64);  // High detail for smooth appearance
    sphere.computeTangents();

    // Load Earth texture
    auto& material = chain.add<TexturedMaterial>("earthMat");
    material.baseColor("assets/textures/flat_earth_Largest_still.0330.jpg");
    material.roughnessFactor(0.8f);   // Mostly matte surface
    material.metallicFactor(0.0f);    // Non-metallic

    // Compose the scene
    auto& scene = SceneComposer::create(chain, "scene");
    scene.add(&sphere, glm::mat4(1.0f), glm::vec4(1.0f));

    // =========================================================================
    // Camera & Lighting
    // =========================================================================

    // Orbit camera looking at the globe
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0, 0);
    camera.distance(3.0f);
    camera.elevation(0.3f);  // Slightly above equator
    camera.azimuth(0.0f);
    camera.fov(45.0f);

    // Key light (simulating sun)
    auto& sunLight = chain.add<DirectionalLight>("sun");
    sunLight.direction(1.0f, 0.5f, 1.0f);
    sunLight.color(1.0f, 0.97f, 0.91f);  // Warm sunlight
    sunLight.intensity = 2.0f;

    // Fill light (ambient bounce)
    auto& fillLight = chain.add<DirectionalLight>("fill");
    fillLight.direction(-1.0f, -0.3f, -0.5f);
    fillLight.color(0.27f, 0.4f, 0.67f);  // Cool blue
    fillLight.intensity = 0.3f;

    // =========================================================================
    // 3D Rendering
    // =========================================================================

    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sunLight);
    render.addLight(&fillLight);
    render.setMaterial(&material);
    render.setShadingMode(ShadingMode::PBR);
    render.setColor(0.02f, 0.02f, 0.04f, 1.0f);  // Dark space background

    // =========================================================================
    // Post-Processing
    // =========================================================================

    // Subtle bloom for atmosphere glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input(&render);
    bloom.threshold = 0.9f;
    bloom.intensity = 0.2f;
    bloom.radius = 6.0f;

    // Vignette for cinematic look
    auto& vignette = chain.add<CRTEffect>("vignette");
    vignette.input(&bloom);
    vignette.curvature = 0.0f;
    vignette.vignette = 0.4f;
    vignette.scanlines = 0.0f;
    vignette.bloom = 0.0f;
    vignette.chromatic = 0.0f;

    chain.output("vignette");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Globe - Vivid 3D Example" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  SPACE: Toggle auto-rotation" << std::endl;
    std::cout << "  TAB: Parameters" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& camera = chain.get<CameraOperator>("camera");
    auto& scene = chain.get<SceneComposer>("scene");

    float dt = static_cast<float>(ctx.dt());

    // Toggle auto-rotation
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        g_autoRotate = !g_autoRotate;
        std::cout << "[globe] Auto-rotate: " << (g_autoRotate ? "ON" : "OFF") << std::endl;
    }

    // Update rotation
    if (g_autoRotate) {
        g_rotation += dt * 0.1f;  // Slow rotation
    }

    // Apply rotation to the globe (rotate around Y axis)
    glm::mat4 earthTransform = glm::rotate(glm::mat4(1.0f), g_rotation, glm::vec3(0, 1, 0));
    // Tilt the Earth ~23.5 degrees like real Earth
    earthTransform = glm::rotate(earthTransform, glm::radians(23.5f), glm::vec3(0, 0, 1));
    scene.entries()[0].transform = earthTransform;

    // Gentle camera bob
    float time = static_cast<float>(ctx.time());
    float elevation = 0.3f + std::sin(time * 0.2f) * 0.05f;
    camera.elevation(elevation);

    // Slow orbit if not auto-rotating globe
    if (!g_autoRotate) {
        camera.azimuth(time * 0.05f);
    }
}

VIVID_CHAIN(setup, update)
