// Textured PBR Demo - Demonstrates PBR rendering with texture maps
// Shows a sphere with bronze material (albedo, normal, metallic, roughness, AO)

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create a sphere with computed tangents for normal mapping
    auto& sphere = chain.add<Sphere>("sphere")
        .radius(1.5f)
        .segments(64)
        .computeTangents();

    // Create scene composer
    auto& scene = chain.add<SceneComposer>("scene");
    scene.add(&sphere);

    // Camera positioned to view the sphere
    auto& camera = chain.add<CameraOperator>("camera")
        .position(0.0f, 1.0f, 4.0f)
        .target(0.0f, 0.0f, 0.0f)
        .fov(45.0f);

    // Directional light from top-right-front
    auto& light = chain.add<DirectionalLight>("sun")
        .direction(1.0f, 1.5f, 1.0f)
        .color(1.0f, 0.98f, 0.95f)
        .intensity(2.5f);

    // Bronze textured material
    auto& material = chain.add<TexturedMaterial>("bronze")
        .baseColor("assets/materials/bronze-bl/bronze_albedo.png")
        .normal("assets/materials/bronze-bl/bronze_normal-ogl.png")
        .metallic("assets/materials/bronze-bl/bronze_metallic.png")
        .roughness("assets/materials/bronze-bl/bronze_roughness.png")
        .ao("assets/materials/bronze-bl/bronze_ao.png")
        // Fallback values (used when texture is missing or multiplied with texture)
        .metallicFactor(1.0f)    // Full metallic from texture
        .roughnessFactor(1.0f)   // Full roughness from texture
        .normalScale(1.0f)       // Full normal map strength
        .aoStrength(1.0f);       // Full AO effect

    // Render with textured PBR
    auto& render = chain.add<Render3D>("render")
        .input(&scene)
        .cameraInput(&camera)
        .lightInput(&light)
        .material(&material)
        .shadingMode(ShadingMode::PBR)
        .clearColor(0.02f, 0.02f, 0.03f);

    chain.output("render");
}

void update(Context& ctx) {
    // Could add camera orbit animation here
}

VIVID_CHAIN(setup, update)
