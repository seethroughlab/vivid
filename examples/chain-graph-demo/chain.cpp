// Chain Graph Demo
// Demonstrates a complex operator chain with 3D rendering and multiple post-processing effects
// Used to test the runtime visualization window that displays the node graph
//
// Chain structure:
//   [bgNoise] ──┬─► [bgColorize] ───────────────────────────────────────────────┐
//   [bgGradient]┘                                                                ├─► [bgComposite] ─┐
//   [Render3D] ─► [HSV] ─► [ChromaticAberration] ─► [Blur] ────────────────────┘                    │
//                                                                                                    ├─► [Composite] ─► [Output]
//   [Noise] ─► [Shape/Vignette] ────────────────────────────────────────────────────────────────────┘

#include <vivid/vivid.h>
#include <vivid/operators.h>
#include <vivid/mesh.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cmath>

using namespace vivid;

// 3D Scene
static std::unique_ptr<Render3D> render3d;
static std::vector<Mesh> meshes;
static std::vector<int> objectIndices;

// Background
static std::unique_ptr<Noise> bgNoise;
static std::unique_ptr<Gradient> bgGradient;
static std::unique_ptr<Composite> bgColorize;
static std::unique_ptr<Composite> bgComposite;

// 2D Post-processing chain
static std::unique_ptr<HSV> hsv;
static std::unique_ptr<ChromaticAberration> chromaAberr;
static std::unique_ptr<Blur> blur;

// Overlay elements
static std::unique_ptr<Noise> noise;
static std::unique_ptr<Shape> vignette;

// Compositing
static std::unique_ptr<Composite> composite;
static std::unique_ptr<Output> output;

static bool initialized = false;
static float rotation = 0.0f;

void setup(Context& ctx) {
    std::cout << "[Chain Graph Demo] Setting up complex operator chain..." << std::endl;

    // ========== 3D SCENE ==========
    render3d = std::make_unique<Render3D>();
    render3d->init(ctx);

    // Create meshes: cube, sphere, torus
    meshes.resize(3);
    objectIndices.resize(3);

    // Cube
    {
        MeshData data = MeshUtils::createCube();
        meshes[0].create(ctx.device(), data);
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.0f, 0.0f));
        objectIndices[0] = render3d->addObject(&meshes[0], transform);
        if (auto* obj = render3d->getObject(objectIndices[0])) {
            obj->color = glm::vec4(0.9f, 0.3f, 0.3f, 1.0f);  // Red
            obj->metallic = 0.8f;
            obj->roughness = 0.2f;
        }
    }

    // Sphere
    {
        MeshData data = MeshUtils::createSphere(32, 16, 0.6f);
        MeshUtils::calculateTangents(data);
        meshes[1].create(ctx.device(), data);
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
        objectIndices[1] = render3d->addObject(&meshes[1], transform);
        if (auto* obj = render3d->getObject(objectIndices[1])) {
            obj->color = glm::vec4(0.3f, 0.9f, 0.4f, 1.0f);  // Green
            obj->metallic = 0.1f;
            obj->roughness = 0.6f;
        }
    }

    // Torus
    {
        MeshData data = MeshUtils::createTorus(32, 16, 0.5f, 0.2f);
        meshes[2].create(ctx.device(), data);
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.0f, 0.0f));
        objectIndices[2] = render3d->addObject(&meshes[2], transform);
        if (auto* obj = render3d->getObject(objectIndices[2])) {
            obj->color = glm::vec4(0.3f, 0.5f, 0.9f, 1.0f);  // Blue
            obj->metallic = 0.9f;
            obj->roughness = 0.1f;
        }
    }

    // Camera
    render3d->camera().setOrbit(glm::vec3(0, 0, 0), 5.0f, 45.0f, 25.0f);

    // Scene settings - transparent background so noise shows through
    render3d->backgroundColor(0.0f, 0.0f, 0.0f, 0.0f);
    render3d->ambientColor(0.15f, 0.15f, 0.2f);

    // Lights
    render3d->addLight(Light3D::directional(
        glm::vec3(-0.5f, -0.8f, -0.5f),
        3.0f,
        glm::vec3(1.0f, 0.95f, 0.9f)
    ));
    render3d->addLight(Light3D::point(
        glm::vec3(2.0f, 2.0f, 3.0f),
        60.0f,
        6.0f,
        glm::vec3(0.8f, 0.9f, 1.0f)
    ));

    // ========== ANIMATED BACKGROUND ==========

    // Noise background with slow animation
    bgNoise = std::make_unique<Noise>();
    bgNoise->init(ctx);
    bgNoise->scale(3.0f)
        .octaves(4)
        .lacunarity(2.0f)
        .persistence(0.5f)
        .speed(0.3f);

    // Gradient for color - purple to blue
    bgGradient = std::make_unique<Gradient>();
    bgGradient->init(ctx);
    bgGradient->type(GradientType::Linear)
        .angle(45.0f)
        .colorA(glm::vec4(0.3f, 0.1f, 0.5f, 1.0f))   // Purple
        .colorB(glm::vec4(0.1f, 0.2f, 0.4f, 1.0f));  // Deep blue

    // Multiply gradient by noise to colorize
    bgColorize = std::make_unique<Composite>();
    bgColorize->init(ctx);
    bgColorize->setInput(0, bgGradient.get());
    bgColorize->setInput(1, bgNoise.get());
    bgColorize->mode(BlendMode::Multiply)
        .opacity(1.0f);

    // ========== 2D POST-PROCESSING ==========

    // HSV adjustment - subtle color grading
    hsv = std::make_unique<HSV>();
    hsv->init(ctx);
    hsv->setInput(render3d.get());
    hsv->hueShift(15.0f)        // Slight warm shift
        .saturation(1.2f)       // Boost saturation
        .value(1.0f);

    // Chromatic aberration - subtle RGB split
    chromaAberr = std::make_unique<ChromaticAberration>();
    chromaAberr->init(ctx);
    chromaAberr->setInput(hsv.get());
    chromaAberr->amount(0.003f)  // Subtle effect
                .center(0.5f, 0.5f);

    // Blur - gentle bloom-like effect
    blur = std::make_unique<Blur>();
    blur->init(ctx);
    blur->setInput(chromaAberr.get());
    blur->radius(2.0f)
        .passes(1);

    // Composite 3D scene over animated background
    bgComposite = std::make_unique<Composite>();
    bgComposite->init(ctx);
    bgComposite->setInput(0, bgColorize.get()); // Colorized background
    bgComposite->setInput(1, blur.get());       // 3D scene on top
    bgComposite->mode(BlendMode::Over)
        .opacity(1.0f);

    // ========== OVERLAY ELEMENTS ==========

    // Animated noise texture for film grain effect
    noise = std::make_unique<Noise>();
    noise->init(ctx);
    noise->scale(300.0f)        // Fine grain
        .octaves(1)
        .speed(60.0f);          // Fast animation for grain effect

    // Vignette shape (dark corners)
    vignette = std::make_unique<Shape>();
    vignette->init(ctx);
    vignette->type(ShapeType::Circle)
        .center(0.5f, 0.5f)
        .radius(0.7f)
        .softness(0.4f)
        .color(glm::vec3(1.0f, 1.0f, 1.0f))
        .backgroundColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.8f));

    // ========== COMPOSITING ==========

    // Composite: background + 3D scene + vignette overlay
    composite = std::make_unique<Composite>();
    composite->init(ctx);
    composite->setInput(0, bgComposite.get());
    composite->setInput(1, vignette.get());
    composite->mode(BlendMode::Multiply)
        .opacity(0.7f);

    // Final output
    output = std::make_unique<Output>();
    output->init(ctx);
    output->setInput(composite.get());

    // Register operators for visualization (press 'V' to toggle)
    ctx.registerOperator("bgNoise", bgNoise.get());
    ctx.registerOperator("bgGradient", bgGradient.get());
    ctx.registerOperator("bgColorize", bgColorize.get());
    ctx.registerOperator("render3d", render3d.get());
    ctx.registerOperator("hsv", hsv.get());
    ctx.registerOperator("chromaAberr", chromaAberr.get());
    ctx.registerOperator("blur", blur.get());
    ctx.registerOperator("bgComposite", bgComposite.get());
    ctx.registerOperator("noise", noise.get());
    ctx.registerOperator("vignette", vignette.get());
    ctx.registerOperator("composite", composite.get());
    ctx.registerOperator("output", output.get());

    initialized = true;

    std::cout << "[Chain Graph Demo] Chain initialized!" << std::endl;
    std::cout << "  Press 'V' to toggle chain visualization" << std::endl;
    std::cout << "  Operators in chain:" << std::endl;
    std::cout << "    Background: bgNoise + bgGradient -> bgColorize" << std::endl;
    std::cout << "    3D Scene: Render3D -> HSV -> ChromaticAberration -> Blur" << std::endl;
    std::cout << "    bgComposite: background + 3D scene" << std::endl;
    std::cout << "    Overlay: Noise (film grain), Vignette" << std::endl;
    std::cout << "    Final: Composite -> Output" << std::endl;
}

void update(Context& ctx) {
    if (!initialized) return;

    float time = ctx.time();
    rotation += ctx.dt() * 0.5f;

    // ========== ANIMATE 3D SCENE ==========

    // Rotate cube
    if (auto* obj = render3d->getObject(objectIndices[0])) {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.0f, 0.0f));
        t = glm::rotate(t, rotation, glm::vec3(0.0f, 1.0f, 0.0f));
        t = glm::rotate(t, rotation * 0.7f, glm::vec3(1.0f, 0.0f, 0.0f));
        obj->transform = t;
    }

    // Bounce sphere
    if (auto* obj = render3d->getObject(objectIndices[1])) {
        float bounce = std::sin(time * 2.0f) * 0.3f;
        glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, bounce, 0.0f));
        t = glm::rotate(t, rotation * 0.3f, glm::vec3(0.0f, 1.0f, 0.0f));
        obj->transform = t;
    }

    // Spin torus
    if (auto* obj = render3d->getObject(objectIndices[2])) {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.0f, 0.0f));
        t = glm::rotate(t, rotation * 1.5f, glm::vec3(1.0f, 0.0f, 0.0f));
        t = glm::rotate(t, rotation, glm::vec3(0.0f, 0.0f, 1.0f));
        obj->transform = t;
    }

    // Slowly orbit camera
    float camAngle = time * 10.0f;
    render3d->camera().setOrbit(glm::vec3(0, 0, 0), 5.0f, camAngle, 20.0f + std::sin(time * 0.3f) * 10.0f);

    // ========== ANIMATE POST-PROCESSING ==========

    // Animate background gradient colors
    float colorPhase = time * 0.3f;
    bgGradient->colorA(glm::vec4(
        0.3f + 0.2f * std::sin(colorPhase),
        0.1f + 0.1f * std::sin(colorPhase * 1.3f),
        0.5f + 0.2f * std::cos(colorPhase * 0.7f),
        1.0f))
    .colorB(glm::vec4(
        0.1f + 0.1f * std::cos(colorPhase * 0.8f),
        0.2f + 0.15f * std::sin(colorPhase * 1.1f),
        0.4f + 0.2f * std::sin(colorPhase * 0.9f),
        1.0f));

    // Animate hue shift over time
    float hueShift = std::sin(time * 0.5f) * 20.0f;
    hsv->hueShift(hueShift);

    // Pulse chromatic aberration
    float aberration = 0.002f + std::sin(time * 0.8f) * 0.001f;
    chromaAberr->amount(aberration);

    // Note: Noise is animated via speed parameter set in setup

    // ========== PROCESS CHAIN ==========

    // Animated background
    bgNoise->process(ctx);
    bgGradient->process(ctx);
    bgColorize->process(ctx);

    // Main 3D pipeline
    render3d->process(ctx);
    hsv->process(ctx);
    chromaAberr->process(ctx);
    blur->process(ctx);

    // Composite 3D over background
    bgComposite->process(ctx);

    // Overlay elements
    noise->process(ctx);
    vignette->process(ctx);

    // Final composite with vignette
    composite->process(ctx);
    output->process(ctx);
}

VIVID_CHAIN(setup, update)
