// Streetlight Fog - Vivid Example
// Volumetric lighting through fog, inspired by ISLANDS: Non-Places
//
// A solitary streetlight in thick fog, with god rays streaming through
// the atmospheric haze. Evokes the liminal, melancholic mood of non-places.

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
    // SCENE - Ground and streetlight geometry
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane - dark asphalt
    auto& ground = scene.add<Box>("ground",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.05f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(20.0f, 0.1f, 20.0f)),
        glm::vec4(0.08f, 0.08f, 0.08f, 1.0f));  // Dark gray asphalt

    // Streetlight pole
    auto& pole = scene.add<Cylinder>("pole",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f)),
        glm::vec4(0.15f, 0.15f, 0.15f, 1.0f));  // Dark metal
    pole.radius(0.05f);
    pole.height(4.0f);
    pole.segments(12);

    // Lamp arm (horizontal piece)
    auto& arm = scene.add<Cylinder>("arm",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.4f, 4.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1)),
        glm::vec4(0.15f, 0.15f, 0.15f, 1.0f));
    arm.radius(0.03f);
    arm.height(0.8f);
    arm.segments(8);

    // Lamp housing (slightly emissive)
    auto& lamp = scene.add<Box>("lamp",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.8f, 3.9f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.15f, 0.1f, 0.15f)),
        glm::vec4(0.2f, 0.18f, 0.15f, 1.0f));

    // =========================================================================
    // CAMERA - Low angle looking up at the streetlight
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(5.0f, 1.5f, 5.0f);
    camera.target(0.0f, 3.0f, 0.0f);
    camera.fov(50.0f);
    camera.nearPlane(0.1f);
    camera.farPlane(100.0f);

    // =========================================================================
    // LIGHTING - Warm streetlight with ambient darkness
    // =========================================================================

    // Main streetlight - point light at the lamp position
    auto& streetlight = chain.add<PointLight>("streetlight");
    streetlight.position(0.8f, 3.85f, 0.0f);
    streetlight.color(1.0f, 0.9f, 0.7f);  // Warm sodium-vapor color
    streetlight.intensity = 1.5f;
    streetlight.range = 12.0f;

    // Very dim ambient (moon/sky)
    auto& ambient = chain.add<DirectionalLight>("ambient");
    ambient.direction(0, -1, 0.5f);
    ambient.color(0.1f, 0.12f, 0.15f);  // Cool blue-gray
    ambient.intensity = 0.2f;

    // =========================================================================
    // RENDER3D - Base scene with depth output for post-processing
    // =========================================================================

    auto& render = chain.add<Render3D>("render3d");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&streetlight);
    render.addLight(&ambient);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.05f);  // Very dark ambient
    render.setClearColor(0.02f, 0.02f, 0.03f);  // Near-black night sky
    render.setResolution(1920, 1080);
    render.setDepthOutput(true);  // Required for volumetric lighting!

    // =========================================================================
    // VOLUMETRIC LIGHTING - God rays through the fog
    // =========================================================================

    auto& volumetric = chain.add<VolumetricLighting>("volumetric");
    volumetric.input(&render);
    volumetric.lightInput(&streetlight);
    volumetric.cameraInput(&camera);

    // Fog/atmosphere parameters
    volumetric.density = 0.015f;     // Subtle fog
    volumetric.intensity = 0.4f;     // Blend strength
    volumetric.fogColor[0] = 0.015f; // Dark blue-gray fog
    volumetric.fogColor[1] = 0.02f;
    volumetric.fogColor[2] = 0.03f;
    volumetric.debugMode = 5;        // 0=off, 1=depth, 2=worldPos, 3=distance, 4=light, 5=passthrough

    chain.output("volumetric");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Subtle camera sway - very slow, dreamlike
    auto& camera = chain.get<CameraOperator>("camera");
    float swayX = std::sin(time * 0.1f) * 0.3f;
    float swayZ = std::cos(time * 0.08f) * 0.2f;
    camera.position(5.0f + swayX, 1.5f, 5.0f + swayZ);

    // Subtle light flicker (sodium lamp effect)
    auto& streetlight = chain.get<PointLight>("streetlight");
    float flicker = 1.0f + std::sin(time * 8.0f) * 0.02f + std::sin(time * 13.0f) * 0.01f;
    streetlight.intensity = 3.0f * flicker;
}

VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = false
}))
