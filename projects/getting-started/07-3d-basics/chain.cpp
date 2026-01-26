// Lesson 07: 3D Basics
// Introduction to 3D rendering with scenes, cameras, and lighting
//
// Run: ./build/bin/vivid projects/getting-started/07-3d-basics

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================
    // SCENE - Container for 3D geometry
    // =========================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Add a cube to the scene
    // Parameters: name, transform (position), color (RGBA)
    scene.add<Box>("cube",
        glm::mat4(1.0f),  // Identity transform (centered at origin)
        glm::vec4(0.8f, 0.3f, 0.2f, 1.0f)  // Red-orange color
    ).size(1.0f, 1.0f, 1.0f);

    // EXPERIMENT: Add more shapes
    // scene.add<Sphere>("sphere",
    //     glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0, 0)),
    //     glm::vec4(0.2f, 0.6f, 0.9f, 1.0f))
    //     .radius(0.5f);

    // =========================================
    // CAMERA - View into the scene
    // =========================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0.0f, 0.0f, 0.0f);  // Look at origin
    camera.distance(4.0f);                  // Distance from center
    camera.elevation(0.4f);                 // Slightly above
    camera.azimuth(0.0f);                   // Starting angle
    camera.fov(50.0f);                      // Field of view

    // =========================================
    // LIGHTING - Illuminate the scene
    // =========================================

    auto& light = chain.add<DirectionalLight>("light");
    light.direction(1, 2, 1);           // Light from upper-right-front
    light.color(1.0f, 1.0f, 1.0f);      // White light
    light.intensity = 1.0f;

    // =========================================
    // RENDER - Combine into final image
    // =========================================

    auto& render = chain.add<Render3D>("render3d");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&light);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.15f);                    // Ambient fill light
    render.setClearColor(0.08f, 0.08f, 0.12f);  // Dark background
    render.setResolution(1920, 1080);

    chain.output("render3d");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Orbit camera around the scene
    auto& camera = chain.get<CameraOperator>("camera");
    camera.azimuth(time * 0.3f);

    // Rotate the cube
    auto& scene = chain.get<SceneComposer>("scene");
    auto& entries = scene.entries();

    // Spin the cube around Y and X axes
    entries[0].transform =
        glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0)) *
        glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(1, 0, 0));
}

VIVID_CHAIN(setup, update)
