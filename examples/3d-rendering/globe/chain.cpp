// Globe - Vivid 3D Example
// A rotating Earth with PBR lighting and procedural noise displacement
//
// Controls:
//   SPACE: Toggle auto-rotation
//   D: Toggle displacement
//   UP/DOWN: Adjust displacement amplitude
//   TAB: Open parameter controls

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

static bool g_autoRotate = true;
static float g_rotation = 0.0f;  // Start facing camera
static bool g_displacementEnabled = true;
static float g_displacementAmplitude = 0.25f;  // More prominent terrain displacement

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Earth Sphere
    // =========================================================================

    // Load Earth texture
    auto& material = chain.add<TexturedMaterial>("earthMat");
    material.baseColor("assets/textures/flat_earth_Largest_still.0330.jpg");
    material.roughnessFactor(0.75f);  // More matte, less shiny
    material.metallicFactor(0.0f);    // Non-metallic

    // Create a high-detail sphere for the globe
    auto& sphere = chain.add<Sphere>("earth");
    sphere.radius(1.0f);
    sphere.segments(128);  // Very high detail for displacement
    sphere.computeTangents();
    sphere.setMaterial(&material);  // Material assigned directly to mesh

    // =========================================================================
    // Displacement Noise
    // =========================================================================

    // Procedural noise for terrain displacement
    auto& noise = chain.add<Noise>("terrain");
    noise.scale = 3.0f;           // Larger features for terrain
    noise.speed = 0.3f;           // Visible animation
    noise.octaves = 4;            // Multi-octave for natural look
    noise.type(NoiseType::Simplex);
    noise.setResolution(512, 512);

    // Compose the scene
    auto& scene = SceneComposer::create(chain, "scene");
    scene.add(&sphere);

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
    sunLight.direction(0.5f, 0.3f, 1.0f);  // More frontal lighting
    sunLight.color(1.0f, 0.97f, 0.91f);  // Warm sunlight
    sunLight.intensity = 4.0f;

    // Fill light (ambient bounce)
    auto& fillLight = chain.add<DirectionalLight>("fill");
    fillLight.direction(-1.0f, -0.3f, -0.5f);
    fillLight.color(0.27f, 0.4f, 0.67f);  // Cool blue
    fillLight.intensity = 1.0f;

    // Rim light (edge separation from dark background)
    auto& rimLight = chain.add<DirectionalLight>("rim");
    rimLight.direction(-0.5f, 0.0f, -1.0f);  // From behind
    rimLight.color(0.6f, 0.7f, 1.0f);        // Cool blue-white
    rimLight.intensity = 1.5f;

    // =========================================================================
    // 3D Rendering
    // =========================================================================

    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sunLight);
    render.addLight(&fillLight);
    render.addLight(&rimLight);
    render.setShadingMode(ShadingMode::PBR);
    render.setColor(0.02f, 0.02f, 0.04f, 1.0f);  // Dark space background

    // Procedural noise displacement for terrain effect
    render.setDisplacementInput(&noise);
    render.setDisplacementAmplitude(g_displacementAmplitude);
    render.setDisplacementMidpoint(0.5f);  // Center value produces no displacement

    // =========================================================================
    // Post-Processing
    // =========================================================================

    // Subtle bloom for atmosphere glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("render");
    bloom.threshold = 0.9f;
    bloom.intensity = 0.2f;
    bloom.radius = 6.0f;

    // Vignette for cinematic look
    auto& vignette = chain.add<CRTEffect>("vignette");
    vignette.input("bloom");
    vignette.curvature = 0.0f;
    vignette.vignette = 0.4f;
    vignette.scanlines = 0.0f;
    vignette.bloom = 0.0f;
    vignette.chromatic = 0.0f;

    chain.output("vignette");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Globe - Vivid 3D Example" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Procedural noise displacement for terrain" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  SPACE: Toggle auto-rotation" << std::endl;
    std::cout << "  D: Toggle displacement" << std::endl;
    std::cout << "  UP/DOWN: Adjust amplitude" << std::endl;
    std::cout << "  TAB: Parameters" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& camera = chain.get<CameraOperator>("camera");
    auto& scene = chain.get<SceneComposer>("scene");
    auto& render = chain.get<Render3D>("render");
    auto& noise = chain.get<Noise>("terrain");

    float dt = static_cast<float>(ctx.dt());

    // Toggle auto-rotation
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        g_autoRotate = !g_autoRotate;
        std::cout << "[globe] Auto-rotate: " << (g_autoRotate ? "ON" : "OFF") << std::endl;
    }

    // Toggle displacement
    if (ctx.key(GLFW_KEY_D).pressed) {
        g_displacementEnabled = !g_displacementEnabled;
        if (g_displacementEnabled) {
            render.setDisplacementInput(&noise);
        } else {
            render.setDisplacementInput(nullptr);
        }
        std::cout << "[globe] Displacement: " << (g_displacementEnabled ? "ON" : "OFF") << std::endl;
    }

    // Adjust displacement amplitude
    if (ctx.key(GLFW_KEY_UP).held) {
        g_displacementAmplitude = std::min(g_displacementAmplitude + dt * 0.1f, 0.3f);
        render.setDisplacementAmplitude(g_displacementAmplitude);
    }
    if (ctx.key(GLFW_KEY_DOWN).held) {
        g_displacementAmplitude = std::max(g_displacementAmplitude - dt * 0.1f, 0.0f);
        render.setDisplacementAmplitude(g_displacementAmplitude);
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
