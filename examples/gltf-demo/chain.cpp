// GLTF Demo - Load and display a 3D model from a GLTF/GLB file
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Load a GLTF model with textures
    auto& model = chain.add<GLTFLoader>("model")
        .file("assets/models/DamagedHelmet.glb")
        .loadTextures(true)
        .scale(1.0f);

    // Create scene with the model
    // Material is automatically picked up from GLTFLoader via outputMaterial()
    auto& scene = SceneComposer::create(chain, "scene");
    scene.add(&model, glm::mat4(1.0f), glm::vec4(1.0f));

    // Camera
    auto& camera = chain.add<CameraOperator>("camera")
        .orbitCenter(0, 0, 0)
        .distance(3.0f)
        .elevation(0.2f)
        .fov(50.0f);

    // Lighting
    auto& sun = chain.add<DirectionalLight>("sun")
        .direction(1, 2, 1)
        .color(1.0f, 0.98f, 0.95f)
        .intensity(1.5f);

    // Render with PBR
    auto& render = chain.add<Render3D>("render")
        .input(&scene)
        .cameraInput(&camera)
        .lightInput(&sun)
        .shadingMode(ShadingMode::PBR)
        .metallic(0.0f)
        .roughness(0.5f)
        .clearColor(0.1f, 0.1f, 0.15f);

    chain.output("render");
}

void update(Context& ctx) {
    // Slowly orbit camera
    auto& camera = ctx.chain().get<CameraOperator>("camera");
    camera.azimuth(ctx.time() * 0.3f);

    // V key toggles vsync
    if (ctx.key(GLFW_KEY_V).pressed) {
        ctx.vsync(!ctx.vsync());
    }
}

VIVID_CHAIN(setup, update)
