// Geometry Showcase - Vivid Example
// Demonstrates all procedural geometry primitives and CSG operations
// Using the new SceneComposer::create() API for clean geometry management

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Persistent state across hot-reloads
static Camera3D camera;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    float spacing = 2.2f;
    float topRowY = 1.5f;
    float bottomRowY = -1.5f;

    // =========================================================================
    // SCENE COMPOSER - Entry point for all geometry
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // -------------------------------------------------------------------------
    // Top row: Basic primitives (created via scene.add<T>())
    // -------------------------------------------------------------------------

    scene.add<BoxGeometry>("box",
        glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 2.5f, topRowY, 0.0f)),
        glm::vec4(0.9f, 0.3f, 0.3f, 1.0f))  // Red
        .size(1.0f)
        .flatShading(true);

    scene.add<SphereGeometry>("sphere",
        glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 1.5f, topRowY, 0.0f)),
        glm::vec4(0.3f, 0.9f, 0.4f, 1.0f))  // Green
        .radius(0.6f)
        .segments(32);

    scene.add<CylinderGeometry>("cylinder",
        glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 0.5f, topRowY, 0.0f)),
        glm::vec4(0.3f, 0.5f, 0.9f, 1.0f))  // Blue
        .radius(0.5f)
        .height(1.2f)
        .segments(24)
        .flatShading(true);

    scene.add<ConeGeometry>("cone",
        glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 0.5f, topRowY, 0.0f)),
        glm::vec4(0.9f, 0.7f, 0.2f, 1.0f))  // Orange
        .radius(0.6f)
        .height(1.2f)
        .segments(24)
        .flatShading(true);

    scene.add<TorusGeometry>("torus",
        glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 1.5f, topRowY, 0.0f)),
        glm::vec4(0.8f, 0.3f, 0.8f, 1.0f))  // Purple
        .outerRadius(0.5f)
        .innerRadius(0.2f)
        .segments(32)
        .rings(16);

    scene.add<PlaneGeometry>("plane",
        glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 2.5f, topRowY, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(-30.0f), glm::vec3(1, 0, 0)),
        glm::vec4(0.2f, 0.8f, 0.8f, 1.0f))  // Cyan
        .size(1.5f, 1.5f)
        .subdivisions(4, 4)
        .flatShading(true);

    // -------------------------------------------------------------------------
    // Bottom row: CSG operations
    // CSG inputs are created via chain.add<>() (not added to scene)
    // CSG results are added to scene via scene.add()
    // -------------------------------------------------------------------------

    // CSG Subtract: Hollow cube
    auto& hollowBox = chain.add<BoxGeometry>("hollowBox").size(1.2f).flatShading(true);
    auto& hollowSphere = chain.add<SphereGeometry>("hollowSphere").radius(0.8f).segments(24);
    auto& csgSubtract = chain.add<Boolean>("csgSubtract")
        .inputA(&hollowBox)
        .inputB(&hollowSphere)
        .operation(BooleanOp::Subtract)
        .flatShading(true);

    scene.add(&csgSubtract,
        glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 0.5f, bottomRowY, 0.0f)),
        glm::vec4(0.4f, 0.8f, 1.0f, 1.0f));  // Light blue

    // CSG Pipe: Cylinder with hole
    auto& outerCyl = chain.add<CylinderGeometry>("outerCyl").radius(0.5f).height(1.5f).segments(32);
    auto& innerCyl = chain.add<CylinderGeometry>("innerCyl").radius(0.3f).height(1.8f).segments(32);
    auto& pipe = chain.add<Boolean>("pipe")
        .inputA(&outerCyl)
        .inputB(&innerCyl)
        .operation(BooleanOp::Subtract)
        .flatShading(true);

    scene.add(&pipe,
        glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 0.5f, bottomRowY, 0.0f)),
        glm::vec4(0.9f, 0.5f, 0.7f, 1.0f));  // Pink

    // =========================================================================
    // RENDER3D - Render scene to texture
    // =========================================================================

    camera.lookAt(glm::vec3(0, 1, 12), glm::vec3(0, 0, 0))
          .fov(50.0f)
          .nearPlane(0.1f)
          .farPlane(100.0f);

    auto& render = chain.add<Render3D>("render3d")
        .input(&scene)
        .camera(camera)
        .shadingMode(ShadingMode::Flat)
        .lightDirection(glm::normalize(glm::vec3(1, 2, 1)))
        .lightColor(glm::vec3(1, 1, 1))
        .ambient(0.2f)
        .clearColor(0.08f, 0.08f, 0.12f)
        .resolution(1280, 720);

    chain.output("render3d");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Gentle camera orbit
    float distance = 14.0f;
    float azimuth = time * 0.15f;
    float elevation = 0.25f;
    camera.orbit(distance, azimuth, elevation);

    auto& render = chain.get<Render3D>("render3d");
    render.camera(camera);

    // Animate objects via SceneComposer entries
    auto& scene = chain.get<SceneComposer>("scene");
    auto& entries = scene.entries();

    float spacing = 2.2f;
    float topRowY = 1.5f;
    float bottomRowY = -1.5f;

    // Top row: Basic primitives
    entries[0].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 2.5f, topRowY, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0));

    float sphereScale = 1.0f + 0.1f * std::sin(time * 2.0f);
    entries[1].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 1.5f, topRowY, 0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(sphereScale));

    entries[2].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 0.5f, topRowY, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.7f, glm::vec3(0, 1, 0));

    entries[3].transform = glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 0.5f, topRowY, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), 0.3f * std::sin(time * 1.5f), glm::vec3(1, 0, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(0, 1, 0));

    entries[4].transform = glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 1.5f, topRowY, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.6f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(1, 0, 0));

    entries[5].transform = glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 2.5f, topRowY, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(-30.0f), glm::vec3(1, 0, 0));

    // Bottom row: CSG operations
    entries[6].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 0.5f, bottomRowY, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.2f, glm::vec3(1, 0, 0));

    entries[7].transform = glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 0.5f, bottomRowY, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 0, 1));
}

VIVID_CHAIN(setup, update)
