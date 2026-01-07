// 3D Orbit - Vivid Project
// Rotating 3D object with PBR rendering

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create a torus mesh
    auto& mesh = chain.add<Torus>("mesh");
    mesh.outerRadius(1.0f);
    mesh.innerRadius(0.4f);
    mesh.segments(48);
    mesh.rings(24);

    // Scene composition
    auto& scene = SceneComposer::create(chain, "scene");
    scene.add(&mesh, glm::mat4(1.0f), glm::vec4(0.8f, 0.2f, 0.1f, 1.0f));

    // Light
    auto& light = chain.add<DirectionalLight>("light");
    light.direction(0.5f, -1.0f, 0.3f);
    light.color(1.0f, 0.95f, 0.9f);
    light.intensity = 2.0f;

    // Camera
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0, 0);
    camera.distance(5.0f);
    camera.elevation(0.3f);
    camera.fov(50.0f);

    // Render
    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&light);
    render.setShadingMode(ShadingMode::PBR);
    render.setMetallic(0.1f);
    render.setRoughness(0.4f);
    render.setColor(0.1f, 0.1f, 0.15f, 1.0f);

    chain.output("render");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Orbit camera
    auto& camera = chain.get<CameraOperator>("camera");
    camera.azimuth(time * 0.5f);

    // Bob elevation
    camera.elevation(0.3f + std::sin(time * 0.3f) * 0.1f);
}

VIVID_CHAIN(setup, update)
