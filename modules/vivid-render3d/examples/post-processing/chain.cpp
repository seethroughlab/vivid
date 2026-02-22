/**
 * Post-Processing Example
 *
 * Demonstrates: Fog, DepthOfField, DepthMask, Particles3D
 *
 * Depth-based post-processing chain with atmospheric fog, depth of
 * field blur, masked bloom effects, and 3D particle fire.
 * All depth-based effects require Render3D::setDepthOutput(true).
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
    // Scene Geometry - Objects at varying depths for post-processing demo
    // =========================================================================

    auto& ground = chain.add<Plane>("ground");
    ground.size(20.0f, 20.0f);

    auto& nearBox = chain.add<Box>("nearBox");
    nearBox.size(1.0f, 1.0f, 1.0f);

    auto& midSphere = chain.add<Sphere>("midSphere");
    midSphere.radius(0.7f);
    midSphere.segments(32);

    auto& farCylinder = chain.add<Cylinder>("farCylinder");
    farCylinder.radius(0.5f);
    farCylinder.height(2.0f);
    farCylinder.segments(16);

    auto& farSphere = chain.add<Sphere>("farSphere");
    farSphere.radius(0.6f);
    farSphere.segments(24);

    // =========================================================================
    // Scene Composition
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane
    scene.add(&ground,
        glm::translate(glm::mat4(1.0f), glm::vec3(0, -1.0f, 0)),
        glm::vec4(0.5f, 0.5f, 0.45f, 1.0f));

    // Near object (close to camera) - will be blurred by DOF
    scene.add(&nearBox,
        glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.0f, 2.0f)),
        glm::vec4(0.9f, 0.4f, 0.3f, 1.0f));  // Red

    // Mid object (at focus distance) - stays sharp
    scene.add(&midSphere,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -3.0f)),
        glm::vec4(0.3f, 0.8f, 0.4f, 1.0f));  // Green

    // Far objects - will be fogged and blurred
    scene.add(&farCylinder,
        glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, -10.0f)),
        glm::vec4(0.4f, 0.5f, 0.9f, 1.0f));  // Blue

    scene.add(&farSphere,
        glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, -18.0f)),
        glm::vec4(0.9f, 0.8f, 0.3f, 1.0f));  // Yellow

    // =========================================================================
    // Lighting
    // =========================================================================

    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(0.5f, -1.0f, 0.3f);
    sun.color(1.0f, 0.95f, 0.9f);
    sun.intensity = 1.5f;

    // =========================================================================
    // Camera
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(0, 2, 6);
    camera.target(0, 0, -5);
    camera.fov(55.0f);
    camera.nearPlane(0.1f);
    camera.farPlane(80.0f);

    // =========================================================================
    // Render3D with depth output (REQUIRED for post-processing)
    // =========================================================================

    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sun);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.2f);
    render.setDepthOutput(true);  // Critical: enables depth buffer access
    render.setClearColor(0.55f, 0.6f, 0.65f, 1.0f);  // Match fog color

    // =========================================================================
    // Fog - Distance-based atmospheric haze
    // =========================================================================

    auto& fog = chain.add<Fog>("fog");
    fog.input(&render);               // Takes color + depth from Render3D
    fog.fogColor[0] = 0.55f;          // Match render clear color
    fog.fogColor[1] = 0.6f;
    fog.fogColor[2] = 0.65f;
    fog.fogStart = 8.0f;              // Fog begins at 8 units
    fog.fogEnd = 30.0f;               // Fully fogged at 30 units
    fog.fogMode = FogMode::Linear;

    // =========================================================================
    // DepthOfField - Focus-based blur
    // =========================================================================

    auto& dof = chain.add<DepthOfField>("dof");
    dof.input(&render);               // Takes color + depth
    dof.focusDistance(0.15f);          // Focus on mid-distance objects
    dof.focusRange(0.1f);             // Narrow sharp zone
    dof.blurStrength(0.6f);           // Moderate blur

    // =========================================================================
    // DepthMask - Restrict bloom to 3D objects only
    // =========================================================================

    // Create a glow effect
    auto& glow = chain.add<Shape>("glow");
    glow.type = ShapeType::Ellipse;
    glow.position.set(0.5f, 0.5f);
    glow.size.set(0.6f, 0.6f);
    glow.color.set(1.0f, 0.8f, 0.4f, 0.5f);
    glow.softness = 0.8f;

    // Mask glow to only appear on 3D objects (not background)
    auto& mask = chain.add<DepthMask>("mask");
    mask.input("glow");
    mask.setRender3D(&render);
    mask.mode(DepthMaskMode::Object);   // Visible only where objects are
    mask.threshold = 0.95f;
    mask.softness = 0.3f;

    // =========================================================================
    // Particles3D - Fire emitter
    // =========================================================================

    auto& fire = chain.add<Particles3D>("fire");
    fire.setCameraInput(&camera);          // Required for billboard orientation
    fire.emitter(Emitter3DShape::Cone);
    fire.position(0.0f, -0.5f, -3.0f);    // At base of mid sphere
    fire.emitterDirection(0.0f, 1.0f, 0.0f);  // Emit upward
    fire.coneAngle(15.0f);
    fire.emitRate(80.0f);
    fire.maxParticles(2000);
    fire.velocity(0.0f, 1.5f, 0.0f);      // Upward
    fire.spread(20.0f);
    fire.gravity(0.0f, -0.3f, 0.0f);      // Light gravity
    fire.life(1.5f);
    fire.lifeVariation(0.3f);
    fire.size(0.3f, 0.0f);                // Shrink over lifetime
    fire.color(1.0f, 0.6f, 0.1f, 1.0f);   // Orange
    fire.colorEnd(1.0f, 0.1f, 0.0f, 0.0f); // Fade to transparent red
    fire.additive(true);                    // Additive blending for glow

    // =========================================================================
    // Composite: combine post-processed scene with particles and mask
    // =========================================================================

    // Layer fog output with masked glow
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("fog");
    comp1.inputB("mask");
    comp1.mode = BlendMode::Add;

    // Layer depth of field on top (blends the DOF effect)
    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA("comp1");
    comp2.inputB("fire");
    comp2.mode = BlendMode::Add;

    chain.output("comp2");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Subtle camera sway
    auto& camera = chain.get<CameraOperator>("camera");
    float swayX = std::sin(time * 0.15f) * 0.5f;
    float swayY = 2.0f + std::sin(time * 0.1f) * 0.3f;
    camera.position(swayX, swayY, 6.0f);

    // Animate near box rotation
    auto& scene = chain.get<SceneComposer>("scene");
    auto& entries = scene.entries();
    if (entries.size() > 1) {
        entries[1].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.0f, 2.0f)) *
                               glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(0, 1, 0));
    }

    // Animate fire flicker
    auto& fire = chain.get<Particles3D>("fire");
    fire.emitRate(60.0f + 30.0f * std::sin(time * 2.0f));
}

VIVID_CHAIN(setup, update)
