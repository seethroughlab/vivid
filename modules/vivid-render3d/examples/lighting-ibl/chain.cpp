/**
 * Lighting & IBL Example
 *
 * Demonstrates: PointLight, SpotLight, IBLEnvironment
 *
 * Scene with multiple light types showing colored point lights,
 * animated spot light, and image-based environment lighting with
 * reflections on PBR surfaces.
 */

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
    // Geometry
    // =========================================================================

    auto& groundPlane = chain.add<Plane>("ground");
    groundPlane.size(12.0f, 12.0f);

    auto& centerSphere = chain.add<Sphere>("centerSphere");
    centerSphere.radius(0.8f);
    centerSphere.segments(32);

    auto& leftSphere = chain.add<Sphere>("leftSphere");
    leftSphere.radius(0.5f);
    leftSphere.segments(32);

    auto& rightSphere = chain.add<Sphere>("rightSphere");
    rightSphere.radius(0.5f);
    rightSphere.segments(32);

    auto& pillar = chain.add<Cylinder>("pillar");
    pillar.radius(0.2f);
    pillar.height(2.0f);
    pillar.segments(16);

    // =========================================================================
    // Scene Composition
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane (neutral gray to show colored lighting)
    scene.add(&groundPlane,
        glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.5f, 0)),
        glm::vec4(0.6f, 0.6f, 0.6f, 1.0f));

    // Center sphere (bright white for clear reflections)
    scene.add(&centerSphere,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.3f, 0.0f)),
        glm::vec4(0.95f, 0.95f, 0.95f, 1.0f));

    // Left sphere (reddish tint)
    scene.add(&leftSphere,
        glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.0f, 0.0f)),
        glm::vec4(0.9f, 0.6f, 0.5f, 1.0f));

    // Right sphere (bluish tint)
    scene.add(&rightSphere,
        glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f)),
        glm::vec4(0.5f, 0.6f, 0.9f, 1.0f));

    // Pillar behind center
    scene.add(&pillar,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, -2.0f)),
        glm::vec4(0.8f, 0.8f, 0.75f, 1.0f));

    // =========================================================================
    // Lighting - Three types demonstrated
    // =========================================================================

    // PointLight: warm orange orbiting light
    auto& pointLight = chain.add<PointLight>("warmPoint");
    pointLight.position(3.0f, 2.0f, 0.0f);
    pointLight.color(1.0f, 0.7f, 0.3f);     // Warm orange
    pointLight.intensity = 8.0f;
    pointLight.range = 15.0f;

    // PointLight: cool blue fill from opposite side
    auto& coolLight = chain.add<PointLight>("coolPoint");
    coolLight.position(-3.0f, 1.5f, 2.0f);
    coolLight.color(0.3f, 0.5f, 1.0f);      // Cool blue
    coolLight.intensity = 5.0f;
    coolLight.range = 12.0f;

    // SpotLight: white spot from above, animated to sweep across scene
    auto& spot = chain.add<SpotLight>("spot");
    spot.position(0.0f, 6.0f, 0.0f);
    spot.direction(0.0f, -1.0f, 0.0f);      // Pointing down
    spot.color(1.0f, 1.0f, 0.95f);          // Near-white
    spot.intensity = 12.0f;
    spot.range = 20.0f;
    spot.spotAngle = 35.0f;                  // Focused cone
    spot.spotBlend = 0.4f;                   // Soft falloff

    // Directional fill light (subtle)
    auto& fill = chain.add<DirectionalLight>("fill");
    fill.direction(0.5f, -1.0f, 0.3f);
    fill.color(1.0f, 1.0f, 1.0f);
    fill.intensity = 0.3f;

    // =========================================================================
    // IBL Environment - Procedural sky for reflections
    // =========================================================================

    auto& ibl = chain.add<IBLEnvironment>("ibl");
    ibl.setUseDefault();  // Procedural sky (no HDR file needed)

    // =========================================================================
    // Camera
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0, 0);
    camera.distance(8.0f);
    camera.elevation(0.4f);
    camera.azimuth(0.0f);
    camera.fov(50.0f);

    // =========================================================================
    // Render3D with IBL and all lights
    // =========================================================================

    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&pointLight);     // Primary light
    render.addLight(&coolLight);           // Additional lights
    render.addLight(&spot);
    render.addLight(&fill);
    render.setShadingMode(ShadingMode::PBR);
    render.setAmbient(0.15f);
    render.setIbl(true);                   // Enable IBL reflections
    render.setEnvironmentInput(&ibl);
    render.setShowSkybox(true);            // Show environment as background
    render.setClearColor(0.05f, 0.05f, 0.08f, 1.0f);

    // Bloom for light highlights
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.setInput(0, &render);
    bloom.threshold = 0.9f;
    bloom.intensity = 0.3f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Orbit camera slowly
    auto& camera = chain.get<CameraOperator>("camera");
    camera.azimuth(time * 0.15f);

    // Animate warm point light in a circle
    auto& pointLight = chain.get<PointLight>("warmPoint");
    float px = std::sin(time * 0.4f) * 3.0f;
    float pz = std::cos(time * 0.4f) * 3.0f;
    pointLight.position(px, 2.0f, pz);

    // Animate spot light to sweep across the scene
    auto& spot = chain.get<SpotLight>("spot");
    float spotX = std::sin(time * 0.3f) * 2.5f;
    float spotZ = std::cos(time * 0.3f) * 2.5f;
    spot.position(spotX, 5.0f, spotZ);
    spot.direction(-spotX * 0.3f, -1.0f, -spotZ * 0.3f);  // Point toward center
}

VIVID_CHAIN(setup, update)
