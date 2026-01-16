/**
 * Sweep Geometry Example
 *
 * Demonstrates: Sweep operator - extrude profiles along paths
 *
 * Shows helix with twist, tapered tube, and star-profile ring
 * using various path types and profile shapes.
 */

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Constants
constexpr float PI = 3.14159265359f;
constexpr float TWO_PI = 6.28318530718f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create scene composer
    auto& scene = SceneComposer::create(chain, "scene");

    // 1. Helix with circular profile and twist
    auto& helix = scene.add<Sweep>("helix",
        glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.0f, 0.0f)),
        glm::vec4(0.9f, 0.3f, 0.3f, 1.0f)  // Red
    );
    helix.pathType(SweepPath::Helix);
    helix.pathRadius(0.8f);
    helix.pathHeight(3.0f);
    helix.pathTurns(3.0f);
    helix.pathSegments(64);
    helix.profileRadius(0.12f);
    helix.profileSegments(12);
    helix.twist(TWO_PI);

    // 2. Tapered tube (Line path with scale variation)
    auto& taper = scene.add<Sweep>("taper",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
        glm::vec4(0.3f, 0.9f, 0.3f, 1.0f)  // Green
    );
    taper.pathType(SweepPath::Line);
    taper.pathHeight(2.5f);
    taper.pathSegments(32);
    taper.profileRadius(0.5f);
    taper.profileSegments(16);
    taper.scaleStart(1.0f);
    taper.scaleEnd(0.15f);

    // 3. Star-profile ring (Circle path with star profile)
    auto& starRing = scene.add<Sweep>("starRing",
        glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)),
        glm::vec4(0.3f, 0.6f, 0.9f, 1.0f)  // Blue
    );
    starRing.pathType(SweepPath::Circle);
    starRing.pathRadius(1.0f);
    starRing.pathSegments(48);
    starRing.profileType(SweepProfile::Star);
    starRing.profileRadius(0.2f);

    // 4. Square-profile arc
    auto& arc = scene.add<Sweep>("arc",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -3.0f)),
        glm::vec4(0.9f, 0.7f, 0.2f, 1.0f)  // Gold
    );
    arc.pathType(SweepPath::Arc);
    arc.pathRadius(1.2f);
    arc.arcAngle(PI * 1.5f);  // 270 degrees
    arc.pathSegments(36);
    arc.profileType(SweepProfile::Square);
    arc.profileRadius(0.15f);
    arc.caps(true);

    // Camera setup (orbit mode)
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0.0f, 0.0f, 0.0f);
    camera.distance(10.0f);
    camera.elevation(0.4f);
    camera.azimuth(0.5f);
    camera.fov(50.0f);

    // Directional light
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(-0.5f, -1.0f, -0.3f);
    sun.color(1.0f, 0.98f, 0.95f);
    sun.intensity = 1.2f;

    // Renderer
    auto& render = chain.add<Render3D>("render3d");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sun);
    render.setClearColor(0.08f, 0.09f, 0.12f);
    render.setAmbient(0.25f);

    chain.output("render3d");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = static_cast<float>(ctx.time());

    auto& camera = chain.get<CameraOperator>("camera");
    auto& scene = chain.get<SceneComposer>("scene");

    // Orbit camera slowly around scene
    camera.azimuth(t * 0.15f);
    camera.elevation(0.4f + std::sin(t * 0.3f) * 0.1f);

    // Animate the tapered tube (entry index 1) - gentle rotation
    glm::mat4 taperTransform = glm::rotate(glm::mat4(1.0f), t * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
    scene.setEntryTransform(1, taperTransform);

    // Animate the star ring (entry index 2) - bobbing motion
    float ringY = std::sin(t * 1.5f) * 0.3f;
    glm::mat4 ringTransform = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, ringY, 0.0f));
    ringTransform = glm::rotate(ringTransform, t * 0.3f, glm::vec3(0.0f, 1.0f, 0.0f));
    scene.setEntryTransform(2, ringTransform);

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
