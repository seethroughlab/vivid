// IBL Demo - Image-Based Lighting demonstration
// Shows PBR textured materials with reflections from HDR environment maps
// Controls:
//   Left-click + drag: Orbit camera
//   Scroll wheel: Zoom

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/render3d/ibl_environment.h>
#include <GLFW/glfw3.h>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Camera orbit control
static float cameraAzimuth = 0.0f;           // radians
static float cameraElevation = 0.35f;        // radians (~20 degrees)
static float cameraDistance = 12.0f;
static bool isDragging = false;
static double lastMouseX = 0.0;
static double lastMouseY = 0.0;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // IBL environment (now a proper chain operator)
    auto& ibl = chain.add<IBLEnvironment>("ibl")
        .hdrFile("assets/hdris/bryanston_park_sunrise_4k.hdr");

    // Create scene composer
    auto& scene = SceneComposer::create(chain, "scene");

    // =========================================================================
    // Row 0: Worn Shiny Metal (textured PBR)
    // =========================================================================
    auto& wornMetal = chain.add<TexturedMaterial>("worn_metal")
        .baseColor("assets/materials/worn-shiny-metal-bl/worn-shiny-metal-albedo.png")
        .normal("assets/materials/worn-shiny-metal-bl/worn-shiny-metal-Normal-ogl.png")
        .metallic("assets/materials/worn-shiny-metal-bl/worn-shiny-metal-Metallic.png")
        .roughness("assets/materials/worn-shiny-metal-bl/worn-shiny-metal-Roughness.png")
        .ao("assets/materials/worn-shiny-metal-bl/worn-shiny-metal-ao.png");

    // =========================================================================
    // Row 1: Bronze (textured PBR)
    // =========================================================================
    auto& bronze = chain.add<TexturedMaterial>("bronze")
        .baseColor("assets/materials/bronze-bl/bronze_albedo.png")
        .normal("assets/materials/bronze-bl/bronze_normal-ogl.png")
        .metallic("assets/materials/bronze-bl/bronze_metallic.png")
        .roughness("assets/materials/bronze-bl/bronze_roughness.png")
        .ao("assets/materials/bronze-bl/bronze_ao.png");

    // =========================================================================
    // Row 2: Titanium Scuffed (textured PBR)
    // =========================================================================
    auto& titanium = chain.add<TexturedMaterial>("titanium")
        .baseColor("assets/materials/Titanium-Scuffed-bl/Titanium-Scuffed_basecolor.png")
        .normal("assets/materials/Titanium-Scuffed-bl/Titanium-Scuffed_normal.png")
        .metallic("assets/materials/Titanium-Scuffed-bl/Titanium-Scuffed_metallic.png")
        .roughness("assets/materials/Titanium-Scuffed-bl/Titanium-Scuffed_roughness.png");

    // =========================================================================
    // Row 3: Rock (textured PBR - dielectric)
    // =========================================================================
    auto& rock = chain.add<TexturedMaterial>("rock")
        .baseColor("assets/materials/roughrockface2-bl/roughrockface2_Base_Color.png")
        .normal("assets/materials/roughrockface2-bl/roughrockface2_Normal.png")
        .metallic("assets/materials/roughrockface2-bl/roughrockface2_Metallic.png")
        .roughness("assets/materials/roughrockface2-bl/roughrockface2_Roughness.png")
        .ao("assets/materials/roughrockface2-bl/roughrockface2_Ambient_Occlusion.png");

    // Array of materials for easy iteration
    TexturedMaterial* materials[] = { &wornMetal, &bronze, &titanium, &rock };
    const char* materialNames[] = { "worn_metal", "bronze", "titanium", "rock" };

    // Create sphere grid - one row per material
    const int cols = 3;
    const int rows = 4;
    const float spacing = 3.5f;
    const float startX = -spacing * (cols - 1) / 2.0f;
    const float startZ = -spacing * (rows - 1) / 2.0f;

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            std::string name = std::string(materialNames[row]) + "_" + std::to_string(col);
            glm::vec3 pos(startX + col * spacing, 0.0f, startZ + row * spacing);

            // Add sphere with high segment count for smooth reflections
            scene.add<Sphere>(name,
                glm::translate(glm::mat4(1.0f), pos))
                .radius(1.4f)
                .segments(64)
                .computeTangents();

            // Assign textured material
            scene.entries().back().material = materials[row];
        }
    }

    // Camera - orbit view with mouse control
    auto& camera = chain.add<CameraOperator>("camera")
        .distance(cameraDistance)
        .elevation(cameraElevation)
        .azimuth(cameraAzimuth)
        .target(0.0f, 0.0f, 0.0f)
        .fov(50.0f);

    // Directional light (still used for direct lighting)
    auto& light = chain.add<DirectionalLight>("sun")
        .direction(1.0f, 2.0f, 1.0f)
        .color(1.0f, 1.0f, 1.0f)
        .intensity(1.5f);

    // Render with PBR + IBL
    auto& render = chain.add<Render3D>("render")
        .input(&scene)
        .cameraInput(&camera)
        .lightInput(&light)
        .shadingMode(ShadingMode::PBR)
        .environmentInput(&ibl)
        .ibl(true)
        .ambient(1.0f)  // IBL provides ambient, set to 1.0 for full effect
        .clearColor(0.1f, 0.1f, 0.12f);

    chain.output("render");
}

void update(Context& ctx) {
    // Get mouse position
    glm::vec2 mousePos = ctx.mouse();
    bool leftButtonDown = ctx.mouseButton(0).held;

    if (leftButtonDown) {
        if (isDragging) {
            // Calculate delta and update camera angles (in radians)
            float deltaX = mousePos.x - static_cast<float>(lastMouseX);
            float deltaY = mousePos.y - static_cast<float>(lastMouseY);

            cameraAzimuth += deltaX * 0.005f;   // Radians per pixel
            cameraElevation += deltaY * 0.005f;
        }
        isDragging = true;
    } else {
        isDragging = false;
    }

    lastMouseX = mousePos.x;
    lastMouseY = mousePos.y;

    // Scroll wheel zoom
    glm::vec2 scrollDelta = ctx.scroll();
    if (scrollDelta.y != 0.0f) {
        cameraDistance -= scrollDelta.y * 1.0f;
        cameraDistance = glm::clamp(cameraDistance, 5.0f, 50.0f);
    }

    // Update camera with new angles and distance
    auto& camera = ctx.chain().get<CameraOperator>("camera");
    camera.azimuth(cameraAzimuth);
    camera.elevation(cameraElevation);
    camera.distance(cameraDistance);
}

VIVID_CHAIN(setup, update)
