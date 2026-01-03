// Geometry Showcase - Vivid Example
// Demonstrates all procedural geometry primitives and CSG operations
// Using the new SceneComposer::create() API for clean geometry management
// Press S to toggle smooth/flat shading
// Press SPACE to cycle through shapes (zoom in on each)

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <GLFW/glfw3.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

static bool useFlatShading = true;

// Store pointers to primitives for shading toggle
static Box* boxPtr = nullptr;
static Cylinder* cylinderPtr = nullptr;
static Cone* conePtr = nullptr;
static Plane* planePtr = nullptr;
static Box* hollowBoxPtr = nullptr;
static Boolean* csgSubtractPtr = nullptr;
static Boolean* pipePtr = nullptr;

// Camera focus: -1 = overview, 0-7 = individual shapes
static int focusedShape = -1;
static const int NUM_SHAPES = 8;

// Shape positions (matches the entries order)
static float spacing = 2.2f;
static float topRowY = 1.5f;
static float bottomRowY = -1.5f;

static glm::vec3 getShapePosition(int index) {
    switch (index) {
        case 0: return glm::vec3(-spacing * 2.5f, topRowY, 0.0f);    // Box
        case 1: return glm::vec3(-spacing * 1.5f, topRowY, 0.0f);    // Sphere
        case 2: return glm::vec3(-spacing * 0.5f, topRowY, 0.0f);    // Cylinder
        case 3: return glm::vec3(spacing * 0.5f, topRowY, 0.0f);     // Cone
        case 4: return glm::vec3(spacing * 1.5f, topRowY, 0.0f);     // Torus
        case 5: return glm::vec3(spacing * 2.5f, topRowY, 0.0f);     // Plane
        case 6: return glm::vec3(-spacing * 0.5f, bottomRowY, 0.0f); // CSG Subtract
        case 7: return glm::vec3(spacing * 0.5f, bottomRowY, 0.0f);  // CSG Pipe
        default: return glm::vec3(0.0f);
    }
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Uncomment to start in fullscreen:
    // ctx.fullscreen(true);

    // =========================================================================
    // SCENE COMPOSER - Entry point for all geometry
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // -------------------------------------------------------------------------
    // Top row: Basic primitives (created via scene.add<T>())
    // -------------------------------------------------------------------------

    boxPtr = &scene.add<Box>("box",
        glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 2.5f, topRowY, 0.0f)),
        glm::vec4(0.9f, 0.3f, 0.3f, 1.0f))  // Red
        .size(1.0f)
        .flatShading(useFlatShading);

    scene.add<Sphere>("sphere",
        glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 1.5f, topRowY, 0.0f)),
        glm::vec4(0.3f, 0.9f, 0.4f, 1.0f))  // Green
        .radius(0.6f)
        .segments(32);

    cylinderPtr = &scene.add<Cylinder>("cylinder",
        glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 0.5f, topRowY, 0.0f)),
        glm::vec4(0.3f, 0.5f, 0.9f, 1.0f))  // Blue
        .radius(0.5f)
        .height(1.2f)
        .segments(24)
        .flatShading(useFlatShading);

    conePtr = &scene.add<Cone>("cone",
        glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 0.5f, topRowY, 0.0f)),
        glm::vec4(0.9f, 0.7f, 0.2f, 1.0f))  // Orange
        .radius(0.6f)
        .height(1.2f)
        .segments(24)
        .flatShading(useFlatShading);

    scene.add<Torus>("torus",
        glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 1.5f, topRowY, 0.0f)),
        glm::vec4(0.8f, 0.3f, 0.8f, 1.0f))  // Purple
        .outerRadius(0.5f)
        .innerRadius(0.2f)
        .segments(32)
        .rings(16);

    planePtr = &scene.add<Plane>("plane",
        glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 2.5f, topRowY, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(-30.0f), glm::vec3(1, 0, 0)),
        glm::vec4(0.2f, 0.8f, 0.8f, 1.0f))  // Cyan
        .size(1.5f, 1.5f)
        .subdivisions(4, 4)
        .flatShading(useFlatShading);

    // -------------------------------------------------------------------------
    // Bottom row: CSG operations
    // CSG inputs are created via chain.add<>() (not added to scene)
    // CSG results are added to scene via scene.add()
    // -------------------------------------------------------------------------

    // CSG Subtract: Hollow cube
    hollowBoxPtr = &chain.add<Box>("hollowBox").size(1.2f).flatShading(useFlatShading);
    auto& hollowSphere = chain.add<Sphere>("hollowSphere").radius(0.8f).segments(24);
    csgSubtractPtr = &chain.add<Boolean>("csgSubtract")
        .inputA(hollowBoxPtr)
        .inputB("hollowSphere")
        .operation(BooleanOp::Subtract)
        .flatShading(useFlatShading);

    scene.add(csgSubtractPtr,
        glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 0.5f, bottomRowY, 0.0f)),
        glm::vec4(0.4f, 0.8f, 1.0f, 1.0f));  // Light blue

    // CSG Pipe: Cylinder with hole
    auto& outerCyl = chain.add<Cylinder>("outerCyl").radius(0.5f).height(1.5f).segments(32);
    auto& innerCyl = chain.add<Cylinder>("innerCyl").radius(0.3f).height(1.8f).segments(32);
    pipePtr = &chain.add<Boolean>("pipe")
        .inputA("outerCyl")
        .inputB("innerCyl")
        .operation(BooleanOp::Subtract)
        .flatShading(useFlatShading);

    scene.add(pipePtr,
        glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 0.5f, bottomRowY, 0.0f)),
        glm::vec4(0.9f, 0.5f, 0.7f, 1.0f));  // Pink

    // =========================================================================
    // CAMERA - Orbit camera as a node
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera")
        .orbitCenter(0, 0, 0)
        .distance(14.0f)
        .azimuth(0.0f)
        .elevation(0.25f)
        .fov(50.0f)
        .nearPlane(0.1f)
        .farPlane(100.0f);

    // =========================================================================
    // LIGHT - Directional light as a node
    // =========================================================================

    auto& sun = chain.add<DirectionalLight>("sun")
        .direction(1, 2, 1)
        .color(1.0f, 1.0f, 1.0f)
        .intensity(1.0f);

    // =========================================================================
    // RENDER3D - Render scene to texture
    // =========================================================================

    auto& render = chain.add<Render3D>("render3d")
        .input("scene")
        .cameraInput(&camera)
        .lightInput(&sun)
        .shadingMode(ShadingMode::Flat)
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

    // Toggle shading mode with S key
    if (ctx.key(GLFW_KEY_S).pressed) {
        useFlatShading = !useFlatShading;

        // Update flatShading on primitives that support it
        if (cylinderPtr) cylinderPtr->flatShading(useFlatShading);
        if (conePtr) conePtr->flatShading(useFlatShading);
    }

    // Cycle through shapes with SPACE key
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        focusedShape++;
        if (focusedShape >= NUM_SHAPES) {
            focusedShape = -1;  // Back to overview
        }
    }

    // Update camera based on focused shape
    auto& camera = chain.get<CameraOperator>("camera");
    if (focusedShape == -1) {
        // Overview: show all shapes
        camera.orbitCenter(0, 0, 0);
        camera.distance(14.0f);
        camera.elevation(0.25f);
    } else {
        // Focus on specific shape
        glm::vec3 pos = getShapePosition(focusedShape);
        camera.orbitCenter(pos.x, pos.y, pos.z);
        camera.distance(3.5f);
        camera.elevation(0.3f);
    }

    // Animate objects via SceneComposer entries
    auto& scene = chain.get<SceneComposer>("scene");
    auto& entries = scene.entries();

    // Top row: Basic primitives (with rotation animation)
    entries[0].transform = glm::translate(glm::mat4(1.0f), getShapePosition(0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0));

    entries[1].transform = glm::translate(glm::mat4(1.0f), getShapePosition(1)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(0, 1, 0));

    entries[2].transform = glm::translate(glm::mat4(1.0f), getShapePosition(2)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.7f, glm::vec3(0, 1, 0));

    entries[3].transform = glm::translate(glm::mat4(1.0f), getShapePosition(3)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(0, 1, 0));

    entries[4].transform = glm::translate(glm::mat4(1.0f), getShapePosition(4)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.6f, glm::vec3(0, 1, 0));

    entries[5].transform = glm::translate(glm::mat4(1.0f), getShapePosition(5)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(-30.0f), glm::vec3(1, 0, 0));

    // Bottom row: CSG operations
    entries[6].transform = glm::translate(glm::mat4(1.0f), getShapePosition(6)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(0, 1, 0));

    entries[7].transform = glm::translate(glm::mat4(1.0f), getShapePosition(7)) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 0, 1));

    // V key toggles vsync
    if (ctx.key(GLFW_KEY_V).pressed) {
        ctx.vsync(!ctx.vsync());
    }
}

VIVID_CHAIN(setup, update)
