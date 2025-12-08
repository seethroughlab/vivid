// PBR Demo - Demonstrates physically-based rendering
// Shows metallic-roughness shading with Cook-Torrance BRDF

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Uncomment to start in fullscreen:
    // ctx.fullscreen(true);

    // Create a sphere primitive
    auto& sphere = chain.add<Sphere>("sphere")
        .radius(1.0f)
        .segments(48);

    // Create scene composer with a single sphere
    auto& scene = chain.add<SceneComposer>("scene");
    scene.add(&sphere);

    // Camera operator - positioned to view the sphere
    auto& camera = chain.add<CameraOperator>("camera")
        .position(0.0f, 0.0f, 4.0f)
        .target(0.0f, 0.0f, 0.0f)
        .fov(45.0f);

    // Directional light from top-right
    auto& light = chain.add<DirectionalLight>("sun")
        .direction(1.0f, 1.0f, 1.0f)
        .color(1.0f, 0.98f, 0.95f)
        .intensity(2.0f);

    // Render with PBR
    auto& render = chain.add<Render3D>("render")
        .input(&scene)
        .cameraInput(&camera)
        .lightInput(&light)
        .shadingMode(ShadingMode::PBR)
        .metallic(0.9f)     // Metallic
        .roughness(0.2f)    // Fairly smooth
        .color(0.95f, 0.64f, 0.54f)  // Copper color
        .clearColor(0.05f, 0.05f, 0.08f);

    chain.output("render");
}

void update(Context& ctx) {
    // V key toggles vsync
    if (ctx.key(GLFW_KEY_V).pressed) {
        ctx.vsync(!ctx.vsync());
    }
}

VIVID_CHAIN(setup, update)
