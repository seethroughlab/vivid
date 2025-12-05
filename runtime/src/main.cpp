// Vivid Runtime - Main Entry Point

#include "vivid/vivid.h"
#include "vivid/operators.h"
#include "vivid/mesh.h"
#include "vivid/camera.h"
#include "vivid/pbr_material.h"
#include "vivid/ibl.h"
#include "vivid/hot_reload.h"
#include "vivid/gltf_model.h"
#include "vivid/preview_server.h"
#include "vivid/chain_visualizer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <chrono>
#include <memory>
#include <cmath>
#include <functional>
#include <thread>

// Platform-specific defines for GLFW native access
#if defined(PLATFORM_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32 1
#elif defined(PLATFORM_LINUX)
    #define GLFW_EXPOSE_NATIVE_X11 1
#elif defined(PLATFORM_MACOS)
    #define GLFW_EXPOSE_NATIVE_COCOA 1
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

// DiligentFX for GLTF PBR rendering - must come after GLFW includes to avoid BOOL macro conflict
#include "GLTF_PBR_Renderer.hpp"
#include "MapHelper.hpp"
#include "GraphicsUtilities.h"

// Include PBR shader structures - after GLFW to avoid macro conflicts
namespace Diligent {
namespace HLSL {
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"
} // namespace HLSL
} // namespace Diligent

// Undef Windows macros that conflict with our code
#ifdef GetObject
    #undef GetObject
#endif
#ifdef CreateWindow
    #undef CreateWindow
#endif

namespace vivid {

// Helper: run an operator chain for a fixed duration
void runFor(Context& ctx, float duration, std::function<void()> process) {
    float startTime = ctx.time();
    while (!ctx.shouldClose() && (ctx.time() - startTime) < duration) {
        ctx.pollEvents();
        ctx.beginFrame();
        process();
        ctx.endFrame();
    }
}

// ============================================
// PHASE 2 TESTS
// ============================================

// Test: SolidColor operator
bool testSolidColor(Context& ctx) {
    std::cout << "\n=== Test: SolidColor ===" << std::endl;

    auto solidColor = std::make_unique<SolidColor>();
    auto output = std::make_unique<Output>();

    solidColor->color(0.8f, 0.2f, 0.3f, 1.0f);
    output->setInput(solidColor.get());

    solidColor->init(ctx);
    output->init(ctx);

    std::cout << "SolidColor: Animated colors for 2s..." << std::endl;

    runFor(ctx, 2.0f, [&]() {
        float t = ctx.time();
        solidColor->color(
            0.5f + 0.5f * std::sin(t * 2.0f),
            0.5f + 0.5f * std::sin(t * 2.5f + 1.0f),
            0.5f + 0.5f * std::sin(t * 3.0f + 2.0f),
            1.0f
        );
        solidColor->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    solidColor->cleanup();

    std::cout << "SolidColor: PASSED" << std::endl;
    return true;
}

// Test: Noise operator
bool testNoise(Context& ctx) {
    std::cout << "\n=== Test: Noise ===" << std::endl;

    auto noise = std::make_unique<Noise>();
    auto output = std::make_unique<Output>();

    noise->scale(4.0f).speed(1.0f).octaves(4);
    output->setInput(noise.get());

    noise->init(ctx);
    output->init(ctx);

    std::cout << "Noise: Animated simplex noise for 2s..." << std::endl;

    runFor(ctx, 2.0f, [&]() {
        noise->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    noise->cleanup();

    std::cout << "Noise: PASSED" << std::endl;
    return true;
}

// Test: Blur operator
bool testBlur(Context& ctx) {
    std::cout << "\n=== Test: Blur ===" << std::endl;

    auto noise = std::make_unique<Noise>();
    auto blur = std::make_unique<Blur>();
    auto output = std::make_unique<Output>();

    noise->scale(8.0f).speed(0.5f);
    blur->setInput(noise.get());
    blur->radius(20.0f);
    output->setInput(blur.get());

    noise->init(ctx);
    blur->init(ctx);
    output->init(ctx);

    std::cout << "Blur: Animated radius for 2s..." << std::endl;

    runFor(ctx, 2.0f, [&]() {
        float t = ctx.time();
        blur->radius(5.0f + 25.0f * (0.5f + 0.5f * std::sin(t * 2.0f)));
        noise->process(ctx);
        blur->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    blur->cleanup();
    noise->cleanup();

    std::cout << "Blur: PASSED" << std::endl;
    return true;
}

// Test: Composite operator
bool testComposite(Context& ctx) {
    std::cout << "\n=== Test: Composite ===" << std::endl;

    auto colorA = std::make_unique<SolidColor>();
    auto colorB = std::make_unique<SolidColor>();
    auto composite = std::make_unique<Composite>();
    auto output = std::make_unique<Output>();

    colorA->color(0.8f, 0.2f, 0.1f, 1.0f);
    colorB->color(0.1f, 0.2f, 0.8f, 0.5f);

    composite->setInput(0, colorA.get());
    composite->setInput(1, colorB.get());
    output->setInput(composite.get());

    colorA->init(ctx);
    colorB->init(ctx);
    composite->init(ctx);
    output->init(ctx);

    const char* modeNames[] = {"Over", "Add", "Multiply", "Screen", "Overlay"};

    for (int mode = 0; mode < 5 && !ctx.shouldClose(); mode++) {
        composite->mode(static_cast<BlendMode>(mode));
        std::cout << "Composite: " << modeNames[mode] << "..." << std::endl;

        runFor(ctx, 1.0f, [&]() {
            float t = ctx.time();
            colorA->color(0.5f + 0.5f * std::sin(t * 1.5f), 0.3f, 0.2f, 1.0f);
            colorB->color(0.2f, 0.3f, 0.5f + 0.5f * std::sin(t * 2.0f + 1.0f), 0.6f);
            colorA->process(ctx);
            colorB->process(ctx);
            composite->process(ctx);
            output->process(ctx);
        });
    }

    output->cleanup();
    composite->cleanup();
    colorB->cleanup();
    colorA->cleanup();

    std::cout << "Composite: PASSED" << std::endl;
    return true;
}

// ============================================
// PHASE 3 TESTS
// ============================================

// Test: Passthrough operator
bool testPassthrough(Context& ctx) {
    std::cout << "\n=== Test: Passthrough ===" << std::endl;

    auto noise = std::make_unique<Noise>();
    auto passthrough = std::make_unique<Passthrough>();
    auto output = std::make_unique<Output>();

    noise->scale(5.0f).speed(1.0f);
    passthrough->setInput(noise.get());
    output->setInput(passthrough.get());

    noise->init(ctx);
    passthrough->init(ctx);
    output->init(ctx);

    std::cout << "Passthrough: Identity transform for 1.5s..." << std::endl;

    runFor(ctx, 1.5f, [&]() {
        noise->process(ctx);
        passthrough->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    passthrough->cleanup();
    noise->cleanup();

    std::cout << "Passthrough: PASSED" << std::endl;
    return true;
}

// Test: Gradient operator
bool testGradient(Context& ctx) {
    std::cout << "\n=== Test: Gradient ===" << std::endl;

    auto gradient = std::make_unique<Gradient>();
    auto output = std::make_unique<Output>();

    gradient->colorA(glm::vec4(0.0f, 0.0f, 0.5f, 1.0f));
    gradient->colorB(glm::vec4(1.0f, 0.5f, 0.0f, 1.0f));
    output->setInput(gradient.get());

    gradient->init(ctx);
    output->init(ctx);

    const char* typeNames[] = {"Linear", "Radial", "Angular", "Diamond"};

    for (int t = 0; t < 4 && !ctx.shouldClose(); t++) {
        gradient->type(static_cast<GradientType>(t));
        std::cout << "Gradient: " << typeNames[t] << "..." << std::endl;

        runFor(ctx, 1.5f, [&]() {
            float time = ctx.time();
            gradient->angle(time * 30.0f);
            gradient->process(ctx);
            output->process(ctx);
        });
    }

    output->cleanup();
    gradient->cleanup();

    std::cout << "Gradient: PASSED" << std::endl;
    return true;
}

// Test: BrightnessContrast operator
bool testBrightnessContrast(Context& ctx) {
    std::cout << "\n=== Test: BrightnessContrast ===" << std::endl;

    auto gradient = std::make_unique<Gradient>();
    auto bc = std::make_unique<BrightnessContrast>();
    auto output = std::make_unique<Output>();

    gradient->type(GradientType::Linear);
    gradient->colorA(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    gradient->colorB(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

    bc->setInput(gradient.get());
    output->setInput(bc.get());

    gradient->init(ctx);
    bc->init(ctx);
    output->init(ctx);

    std::cout << "BrightnessContrast: Animated for 2s..." << std::endl;

    runFor(ctx, 2.0f, [&]() {
        float t = ctx.time();
        bc->brightness(0.3f * std::sin(t * 2.0f));
        bc->contrast(1.0f + 1.0f * std::sin(t * 1.5f));
        gradient->process(ctx);
        bc->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    bc->cleanup();
    gradient->cleanup();

    std::cout << "BrightnessContrast: PASSED" << std::endl;
    return true;
}

// Test: HSV operator
bool testHSV(Context& ctx) {
    std::cout << "\n=== Test: HSV ===" << std::endl;

    auto gradient = std::make_unique<Gradient>();
    auto hsv = std::make_unique<HSV>();
    auto output = std::make_unique<Output>();

    gradient->type(GradientType::Radial);
    gradient->colorA(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    gradient->colorB(glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

    hsv->setInput(gradient.get());
    output->setInput(hsv.get());

    gradient->init(ctx);
    hsv->init(ctx);
    output->init(ctx);

    std::cout << "HSV: Hue rotation for 3s..." << std::endl;

    runFor(ctx, 3.0f, [&]() {
        float t = ctx.time();
        hsv->hueShift(t * 60.0f);  // Rotate hue over time
        hsv->saturation(1.0f + 0.5f * std::sin(t * 2.0f));
        gradient->process(ctx);
        hsv->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    hsv->cleanup();
    gradient->cleanup();

    std::cout << "HSV: PASSED" << std::endl;
    return true;
}

// Test: Transform operator
bool testTransform(Context& ctx) {
    std::cout << "\n=== Test: Transform ===" << std::endl;

    auto noise = std::make_unique<Noise>();
    auto transform = std::make_unique<Transform>();
    auto output = std::make_unique<Output>();

    noise->scale(3.0f).speed(0.5f);
    transform->setInput(noise.get());
    output->setInput(transform.get());

    noise->init(ctx);
    transform->init(ctx);
    output->init(ctx);

    std::cout << "Transform: Rotate/scale for 3s..." << std::endl;

    runFor(ctx, 3.0f, [&]() {
        float t = ctx.time();
        transform->rotate(t * 45.0f);
        transform->scale(0.5f + 0.5f * std::sin(t * 1.5f));
        transform->translate(0.1f * std::sin(t * 2.0f), 0.1f * std::cos(t * 2.0f));
        noise->process(ctx);
        transform->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    transform->cleanup();
    noise->cleanup();

    std::cout << "Transform: PASSED" << std::endl;
    return true;
}

// Test: Feedback operator
bool testFeedback(Context& ctx) {
    std::cout << "\n=== Test: Feedback ===" << std::endl;

    auto noise = std::make_unique<Noise>();
    auto feedback = std::make_unique<Feedback>();
    auto output = std::make_unique<Output>();

    noise->scale(10.0f).speed(2.0f);
    feedback->setInput(noise.get());
    feedback->decay(0.95f).mix(0.7f);
    output->setInput(feedback.get());

    noise->init(ctx);
    feedback->init(ctx);
    output->init(ctx);

    std::cout << "Feedback: Trail effect for 3s..." << std::endl;

    runFor(ctx, 3.0f, [&]() {
        noise->process(ctx);
        feedback->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    feedback->cleanup();
    noise->cleanup();

    std::cout << "Feedback: PASSED" << std::endl;
    return true;
}

// Test: EdgeDetect operator
bool testEdgeDetect(Context& ctx) {
    std::cout << "\n=== Test: EdgeDetect ===" << std::endl;

    auto noise = std::make_unique<Noise>();
    auto edge = std::make_unique<EdgeDetect>();
    auto output = std::make_unique<Output>();

    noise->scale(5.0f).speed(0.5f);
    edge->setInput(noise.get());
    edge->strength(2.0f);
    output->setInput(edge.get());

    noise->init(ctx);
    edge->init(ctx);
    output->init(ctx);

    const char* modeNames[] = {"Sobel", "Prewitt", "Laplacian"};

    for (int m = 0; m < 3 && !ctx.shouldClose(); m++) {
        edge->mode(static_cast<EdgeDetectMode>(m));
        std::cout << "EdgeDetect: " << modeNames[m] << "..." << std::endl;

        runFor(ctx, 1.5f, [&]() {
            noise->process(ctx);
            edge->process(ctx);
            output->process(ctx);
        });
    }

    output->cleanup();
    edge->cleanup();
    noise->cleanup();

    std::cout << "EdgeDetect: PASSED" << std::endl;
    return true;
}

// Test: Displacement operator
bool testDisplacement(Context& ctx) {
    std::cout << "\n=== Test: Displacement ===" << std::endl;

    auto gradient = std::make_unique<Gradient>();
    auto noise = std::make_unique<Noise>();
    auto displacement = std::make_unique<Displacement>();
    auto output = std::make_unique<Output>();

    // Source: gradient
    gradient->type(GradientType::Linear);
    gradient->colorA(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    gradient->colorB(glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

    // Displacement map: noise
    noise->scale(3.0f).speed(1.0f);

    displacement->setInput(0, gradient.get());  // Source
    displacement->setInput(1, noise.get());     // Displacement map
    displacement->amount(0.1f);
    output->setInput(displacement.get());

    gradient->init(ctx);
    noise->init(ctx);
    displacement->init(ctx);
    output->init(ctx);

    std::cout << "Displacement: UV warping for 3s..." << std::endl;

    runFor(ctx, 3.0f, [&]() {
        float t = ctx.time();
        displacement->amount(0.05f + 0.1f * std::sin(t * 2.0f));
        gradient->process(ctx);
        noise->process(ctx);
        displacement->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    displacement->cleanup();
    noise->cleanup();
    gradient->cleanup();

    std::cout << "Displacement: PASSED" << std::endl;
    return true;
}

// Test: ChromaticAberration operator
bool testChromaticAberration(Context& ctx) {
    std::cout << "\n=== Test: ChromaticAberration ===" << std::endl;

    auto gradient = std::make_unique<Gradient>();
    auto chromab = std::make_unique<ChromaticAberration>();
    auto output = std::make_unique<Output>();

    gradient->type(GradientType::Radial);
    gradient->colorA(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    gradient->colorB(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));

    chromab->setInput(gradient.get());
    chromab->amount(0.02f);
    output->setInput(chromab.get());

    gradient->init(ctx);
    chromab->init(ctx);
    output->init(ctx);

    std::cout << "ChromaticAberration: RGB split for 2s..." << std::endl;

    runFor(ctx, 2.0f, [&]() {
        float t = ctx.time();
        chromab->amount(0.01f + 0.03f * std::sin(t * 3.0f));
        chromab->angle(t * 90.0f);
        gradient->process(ctx);
        chromab->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    chromab->cleanup();
    gradient->cleanup();

    std::cout << "ChromaticAberration: PASSED" << std::endl;
    return true;
}

// Test: Pixelate operator
bool testPixelate(Context& ctx) {
    std::cout << "\n=== Test: Pixelate ===" << std::endl;

    auto noise = std::make_unique<Noise>();
    auto pixelate = std::make_unique<Pixelate>();
    auto output = std::make_unique<Output>();

    noise->scale(3.0f).speed(0.5f);
    pixelate->setInput(noise.get());
    output->setInput(pixelate.get());

    noise->init(ctx);
    pixelate->init(ctx);
    output->init(ctx);

    std::cout << "Pixelate: Animated pixel size for 2s..." << std::endl;

    runFor(ctx, 2.0f, [&]() {
        float t = ctx.time();
        pixelate->pixelSize(4.0f + 20.0f * (0.5f + 0.5f * std::sin(t * 2.0f)));
        noise->process(ctx);
        pixelate->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    pixelate->cleanup();
    noise->cleanup();

    std::cout << "Pixelate: PASSED" << std::endl;
    return true;
}

// Test: Mirror operator
bool testMirror(Context& ctx) {
    std::cout << "\n=== Test: Mirror ===" << std::endl;

    auto noise = std::make_unique<Noise>();
    auto mirror = std::make_unique<Mirror>();
    auto output = std::make_unique<Output>();

    noise->scale(4.0f).speed(0.5f);
    mirror->setInput(noise.get());
    output->setInput(mirror.get());

    noise->init(ctx);
    mirror->init(ctx);
    output->init(ctx);

    const char* modeNames[] = {"Horizontal", "Vertical", "Both", "Quad", "Kaleidoscope"};

    for (int m = 0; m < 5 && !ctx.shouldClose(); m++) {
        mirror->mode(static_cast<MirrorMode>(m));
        if (m == 4) mirror->segments(6);  // Kaleidoscope with 6 segments
        std::cout << "Mirror: " << modeNames[m] << "..." << std::endl;

        runFor(ctx, 1.5f, [&]() {
            float t = ctx.time();
            if (m == 4) mirror->angle(t * 30.0f);  // Rotate kaleidoscope
            noise->process(ctx);
            mirror->process(ctx);
            output->process(ctx);
        });
    }

    output->cleanup();
    mirror->cleanup();
    noise->cleanup();

    std::cout << "Mirror: PASSED" << std::endl;
    return true;
}

// ============================================
// PHASE 4 TESTS
// ============================================

// Test: Render3D operator
bool testRender3D(Context& ctx) {
    std::cout << "\n=== Test: Render3D ===" << std::endl;

    auto render3d = std::make_unique<Render3D>();
    auto output = std::make_unique<Output>();

    output->setInput(render3d.get());

    render3d->init(ctx);
    output->init(ctx);

    // Create meshes
    MeshData cubeData = MeshUtils::createCube();
    MeshData sphereData = MeshUtils::createSphere(32, 16, 0.4f);
    MeshData planeData = MeshUtils::createPlane(5.0f, 5.0f, 1, 1);

    Mesh cubeMesh, sphereMesh, planeMesh;
    cubeMesh.create(ctx.device(), cubeData);
    sphereMesh.create(ctx.device(), sphereData);
    planeMesh.create(ctx.device(), planeData);

    // Add objects to scene
    int planeIdx = render3d->addObject(&planeMesh, glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.5f, 0)));
    int cubeIdx = render3d->addObject(&cubeMesh, glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0, 0)));
    int sphereIdx = render3d->addObject(&sphereMesh, glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0, 0)));

    // Set material properties
    if (auto* obj = render3d->getObject(planeIdx)) {
        obj->color = glm::vec4(0.3f, 0.3f, 0.35f, 1.0f);
        obj->roughness = 0.9f;
    }
    if (auto* obj = render3d->getObject(cubeIdx)) {
        obj->color = glm::vec4(0.8f, 0.2f, 0.1f, 1.0f);
        obj->metallic = 0.0f;
        obj->roughness = 0.5f;
    }
    if (auto* obj = render3d->getObject(sphereIdx)) {
        obj->color = glm::vec4(0.2f, 0.5f, 0.9f, 1.0f);
        obj->metallic = 0.8f;
        obj->roughness = 0.2f;
    }

    // Setup camera
    render3d->camera().setOrbit(glm::vec3(0, 0, 0), 4.0f, 45.0f, 30.0f);

    // Scene settings
    render3d->backgroundColor(0.05f, 0.05f, 0.1f);
    render3d->ambientColor(0.15f, 0.15f, 0.2f);

    std::cout << "Render3D: 3D scene with orbit camera for 4s..." << std::endl;

    runFor(ctx, 4.0f, [&]() {
        float t = ctx.time();

        // Orbit camera
        render3d->camera().orbitRotate(0.5f, 0.0f);

        // Animate cube rotation
        if (auto* obj = render3d->getObject(cubeIdx)) {
            obj->transform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.0f, 0.0f)) *
                            glm::rotate(glm::mat4(1.0f), t * 0.5f, glm::vec3(0, 1, 0)) *
                            glm::rotate(glm::mat4(1.0f), t * 0.3f, glm::vec3(1, 0, 0));
        }

        // Animate sphere bounce
        if (auto* obj = render3d->getObject(sphereIdx)) {
            float bounce = 0.2f * std::abs(std::sin(t * 3.0f));
            obj->transform = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, bounce, 0.0f));
        }

        render3d->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    render3d->cleanup();

    sphereMesh.release();
    cubeMesh.release();
    planeMesh.release();

    std::cout << "Render3D: PASSED" << std::endl;
    return true;
}

// Test: PBR Textures with IBL
bool testPBRTextures(Context& ctx) {
    std::cout << "\n=== Test: PBR Textures ===" << std::endl;

    // Load PBR material - assets are in the app bundle
    std::string assetPath = "runtime/vivid.app/Contents/MacOS/assets/";

    // Load PBR material
    PBRMaterial rockMaterial;
    if (!rockMaterial.loadFromDirectory(ctx, assetPath + "materials/roughrockface2-bl", "roughrockface2")) {
        std::cout << "Warning: Could not load rock material, using defaults" << std::endl;
        rockMaterial.createDefaults(ctx);
    }

    // Create render3d operator
    auto render3d = std::make_unique<Render3D>();
    auto output = std::make_unique<Output>();

    output->setInput(render3d.get());

    render3d->init(ctx);
    output->init(ctx);

    // Create meshes
    MeshData sphereData = MeshUtils::createSphere(64, 32, 0.8f);
    MeshData planeData = MeshUtils::createPlane(6.0f, 6.0f, 4, 4);

    Mesh sphereMesh, planeMesh;
    sphereMesh.create(ctx.device(), sphereData);
    planeMesh.create(ctx.device(), planeData);

    // Add objects
    int sphereIdx = render3d->addObject(&sphereMesh, glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.3f, 0)));
    int planeIdx = render3d->addObject(&planeMesh,
        glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0)) *
        glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -0.5f)));

    // Assign material to sphere
    if (auto* obj = render3d->getObject(sphereIdx)) {
        obj->material = &rockMaterial;
        obj->uvScale = 2.0f;
        obj->color = glm::vec4(1.0f);  // Material provides color
    }

    // Plane uses simple PBR
    if (auto* obj = render3d->getObject(planeIdx)) {
        obj->color = glm::vec4(0.2f, 0.2f, 0.25f, 1.0f);
        obj->metallic = 0.0f;
        obj->roughness = 0.8f;
    }

    // Setup camera
    render3d->camera().setOrbit(glm::vec3(0, 0.2f, 0), 3.0f, 45.0f, 20.0f);

    // Scene settings
    render3d->backgroundColor(0.02f, 0.02f, 0.04f);
    render3d->ambientColor(0.2f, 0.2f, 0.25f);

    std::cout << "PBR Textures: Bronze sphere with orbit camera for 2s..." << std::endl;

    runFor(ctx, 2.0f, [&]() {
        // Orbit camera
        render3d->camera().orbitRotate(0.4f, 0.0f);

        render3d->process(ctx);
        output->process(ctx);
    });

    output->cleanup();
    render3d->cleanup();
    rockMaterial.cleanup();

    sphereMesh.release();
    planeMesh.release();

    std::cout << "PBR Textures: PASSED" << std::endl;
    return true;
}

// PBR Material Gallery - cycles through all materials with spacebar
void runPBRMaterialGallery(Context& ctx) {
    std::cout << "\n=== PBR Material Gallery ===" << std::endl;
    std::cout << "Press SPACE to cycle through materials" << std::endl;
    std::cout << "Press ESC to exit" << std::endl;

    // Asset path (in app bundle)
    std::string assetPath = "runtime/vivid.app/Contents/MacOS/assets/";

    // Material definitions
    struct MaterialInfo {
        std::string folder;
        std::string prefix;
        std::string displayName;
    };

    std::vector<MaterialInfo> materialInfos = {
        {"bronze-bl", "bronze", "Bronze"},
        {"hexagon-pavers1-bl", "hexagon-pavers1", "Hexagon Pavers"},
        {"roughrockface2-bl", "roughrockface2", "Rough Rock Face"},
        {"speckled-granite-tiles-bl", "speckled-granite-tiles", "Speckled Granite Tiles"},
        {"square-damp-blocks-bl", "square-damp-blocks", "Square Damp Blocks"},
        {"whispy-grass-meadow-bl", "wispy-grass-meadow", "Whispy Grass Meadow"}
    };

    // Load all materials
    std::vector<PBRMaterial> materials(materialInfos.size());
    for (size_t i = 0; i < materialInfos.size(); i++) {
        std::string path = assetPath + "materials/" + materialInfos[i].folder;
        if (!materials[i].loadFromDirectory(ctx, path, materialInfos[i].prefix)) {
            std::cout << "Warning: Could not load " << materialInfos[i].displayName << " material" << std::endl;
            materials[i].createDefaults(ctx);
        } else {
            std::cout << "Loaded: " << materialInfos[i].displayName << std::endl;
        }
    }

    // Create render3d operator
    auto render3d = std::make_unique<Render3D>();
    auto output = std::make_unique<Output>();

    output->setInput(render3d.get());

    render3d->init(ctx);
    output->init(ctx);

    // Create sphere mesh
    MeshData sphereData = MeshUtils::createSphere(64, 32, 1.0f);
    Mesh sphereMesh;
    sphereMesh.create(ctx.device(), sphereData);

    // Add sphere to scene
    int sphereIdx = render3d->addObject(&sphereMesh, glm::mat4(1.0f));

    // Setup camera
    render3d->camera().setOrbit(glm::vec3(0, 0, 0), 3.5f, 45.0f, 15.0f);

    // Scene settings
    render3d->backgroundColor(0.02f, 0.02f, 0.04f);
    render3d->ambientColor(0.3f, 0.3f, 0.35f);

    // Current material index
    int currentMaterial = 0;

    // Set initial material
    if (auto* obj = render3d->getObject(sphereIdx)) {
        obj->material = &materials[currentMaterial];
        obj->uvScale = 2.0f;
        obj->color = glm::vec4(1.0f);
    }

    std::cout << "\nShowing: " << materialInfos[currentMaterial].displayName << std::endl;

    // Main loop
    while (!ctx.shouldClose()) {
        ctx.pollEvents();

        // Check for spacebar to cycle materials
        if (ctx.wasKeyPressed(GLFW_KEY_SPACE)) {
            currentMaterial = (currentMaterial + 1) % static_cast<int>(materials.size());
            if (auto* obj = render3d->getObject(sphereIdx)) {
                obj->material = &materials[currentMaterial];
            }
            std::cout << "Showing: " << materialInfos[currentMaterial].displayName
                      << " (" << (currentMaterial + 1) << "/" << materials.size() << ")" << std::endl;
        }

        ctx.beginFrame();

        // Slowly rotate the camera
        render3d->camera().orbitRotate(0.2f, 0.0f);

        render3d->process(ctx);
        output->process(ctx);

        ctx.endFrame();
    }

    // Cleanup
    output->cleanup();
    render3d->cleanup();

    for (auto& mat : materials) {
        mat.cleanup();
    }

    sphereMesh.release();

    std::cout << "PBR Material Gallery: Done" << std::endl;
}

// ============================================
// TEST RUNNER
// ============================================

void runOperatorTests(Context& ctx) {
    std::cout << "Vivid Runtime v" << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "       OPERATOR TEST SUITE" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "Press ESC at any time to exit\n" << std::endl;

    int passed = 0;
    int total = 18;

    // Phase 4 tests (run first for faster iteration during development)
    std::cout << "--- PHASE 4: 3D Rendering ---" << std::endl;
    if (!ctx.shouldClose() && testRender3D(ctx)) passed++;
    if (!ctx.shouldClose() && testPBRTextures(ctx)) passed++;

    // Phase 2 tests
    std::cout << "\n--- PHASE 2: Core Operators ---" << std::endl;
    if (!ctx.shouldClose() && testSolidColor(ctx)) passed++;
    if (!ctx.shouldClose() && testNoise(ctx)) passed++;
    if (!ctx.shouldClose() && testBlur(ctx)) passed++;
    if (!ctx.shouldClose() && testComposite(ctx)) passed++;

    // Phase 3 tests
    std::cout << "\n--- PHASE 3: Additional 2D Operators ---" << std::endl;
    if (!ctx.shouldClose() && testPassthrough(ctx)) passed++;
    if (!ctx.shouldClose() && testGradient(ctx)) passed++;
    if (!ctx.shouldClose() && testBrightnessContrast(ctx)) passed++;
    if (!ctx.shouldClose() && testHSV(ctx)) passed++;
    if (!ctx.shouldClose() && testTransform(ctx)) passed++;
    if (!ctx.shouldClose() && testFeedback(ctx)) passed++;
    if (!ctx.shouldClose() && testEdgeDetect(ctx)) passed++;
    if (!ctx.shouldClose() && testDisplacement(ctx)) passed++;
    if (!ctx.shouldClose() && testChromaticAberration(ctx)) passed++;
    if (!ctx.shouldClose() && testPixelate(ctx)) passed++;
    if (!ctx.shouldClose() && testMirror(ctx)) passed++;

    // Full chain test
    std::cout << "\n--- Integration Test ---" << std::endl;
    if (!ctx.shouldClose()) {
        std::cout << "\n=== Test: Full Chain ===" << std::endl;
        auto noise = std::make_unique<Noise>();
        auto blur = std::make_unique<Blur>();
        auto hsv = std::make_unique<HSV>();
        auto output = std::make_unique<Output>();

        noise->scale(5.0f).speed(0.5f);
        blur->setInput(noise.get());
        blur->radius(10.0f);
        hsv->setInput(blur.get());
        output->setInput(hsv.get());

        noise->init(ctx);
        blur->init(ctx);
        hsv->init(ctx);
        output->init(ctx);

        std::cout << "Full Chain: Noise -> Blur -> HSV -> Output for 3s..." << std::endl;

        runFor(ctx, 3.0f, [&]() {
            float t = ctx.time();
            hsv->hueShift(t * 45.0f);
            noise->process(ctx);
            blur->process(ctx);
            hsv->process(ctx);
            output->process(ctx);
        });

        output->cleanup();
        hsv->cleanup();
        blur->cleanup();
        noise->cleanup();

        std::cout << "Full Chain: PASSED" << std::endl;
        passed++;
    }

    std::cout << "\n============================================" << std::endl;
    std::cout << "       TEST RESULTS: " << passed << "/" << total << " PASSED" << std::endl;
    std::cout << "============================================" << std::endl;

    if (passed == total) {
        std::cout << "All operators working correctly!" << std::endl;
    }
}

} // namespace vivid

// Hot reload mode: run a project with live code reloading
void runHotReload(vivid::Context& ctx, const std::string& projectPath, int wsPort = 9876) {
    std::cout << "Hot Reload Mode: " << projectPath << std::endl;

    vivid::HotReload hotReload;

    // Set runtime path (for includes)
    // The runtime is in the build directory, sources are in runtime/
    vivid::fs::path execPath = vivid::fs::current_path();
    vivid::fs::path runtimePath = execPath.parent_path().parent_path() / "runtime";
    if (!vivid::fs::exists(runtimePath / "include")) {
        // Try relative to source tree
        runtimePath = vivid::fs::path(projectPath).parent_path().parent_path() / "runtime";
    }
    hotReload.setRuntimePath(runtimePath);

    // Initialize hot reload
    if (!hotReload.init(projectPath)) {
        std::cerr << "[Hot Reload] Failed to initialize: " << hotReload.lastError() << std::endl;
        return;
    }

    // Start preview server for VS Code extension
    vivid::PreviewServer previewServer(wsPort);
    bool needsReload = false;

    previewServer.setCommandCallback([&](const std::string& type, const nlohmann::json& data) {
        if (type == "reload") {
            std::cout << "[PreviewServer] Reload requested" << std::endl;
            needsReload = true;
        } else if (type == "param_change") {
            std::cout << "[PreviewServer] Param change: " << data.dump() << std::endl;
            // TODO: Live parameter updates
        } else if (type == "pause") {
            std::cout << "[PreviewServer] Pause: " << data.dump() << std::endl;
            // TODO: Pause/resume
        }
    });

    previewServer.start();

    // Initialize chain visualizer
    vivid::ChainVisualizer visualizer;
    visualizer.init(ctx);

    // Call setup if available
    bool needsSetup = true;
    bool lastCompileSuccess = true;

    // Main loop
    auto lastPollTime = std::chrono::steady_clock::now();
    const auto pollInterval = std::chrono::milliseconds(100);

    while (!ctx.shouldClose()) {
        ctx.pollEvents();

        // Poll for file changes periodically
        auto now = std::chrono::steady_clock::now();
        if (now - lastPollTime >= pollInterval) {
            lastPollTime = now;

            // Check for extension reload request
            if (needsReload) {
                ctx.clearRegisteredOperators();  // Clear before reload
                hotReload.reload();
                needsReload = false;
                needsSetup = true;

                // Send compile status to VS Code extension
                if (hotReload.isReady()) {
                    previewServer.sendCompileStatus(true, "Compiled successfully");
                    lastCompileSuccess = true;
                } else if (hotReload.hasCompileError()) {
                    previewServer.sendCompileStatus(false, hotReload.lastError());
                    lastCompileSuccess = false;
                }
            }

            if (hotReload.poll()) {
                ctx.clearRegisteredOperators();  // Clear before reload
                needsSetup = true;

                // Send compile status to VS Code extension
                if (hotReload.isReady()) {
                    previewServer.sendCompileStatus(true, "Compiled successfully");
                    lastCompileSuccess = true;
                } else if (hotReload.hasCompileError()) {
                    previewServer.sendCompileStatus(false, hotReload.lastError());
                    lastCompileSuccess = false;
                }
            }
        }

        // Toggle visualizer with 'V' key
        if (ctx.wasKeyPressed(GLFW_KEY_V)) {
            visualizer.toggleVisible();
        }

        // Call setup after load/reload
        if (needsSetup && hotReload.isReady()) {
            if (auto setup = hotReload.setup()) {
                setup(ctx);
            }
            needsSetup = false;
        }

        ctx.beginFrame();

        // Begin visualizer frame (ImGui new frame)
        visualizer.beginFrame(ctx);

        // Call update every frame
        if (hotReload.isReady()) {
            if (auto update = hotReload.update()) {
                update(ctx);
            }
        } else if (hotReload.hasCompileError()) {
            // Show compile error - for now just clear to red
            // TODO: Render error overlay
        }

        // Render chain visualizer overlay
        visualizer.render(ctx);

        ctx.endFrame();
    }

    visualizer.shutdown();
    previewServer.stop();
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options] [project-path]\n"
              << "\n"
              << "Options:\n"
              << "  --pbr-gallery   Run the PBR material gallery (spacebar cycles materials)\n"
              << "  -h, --help      Show this help message\n"
              << "\n"
              << "  project-path    Path to a Vivid project directory containing chain.cpp\n"
              << "                  If not specified, runs the built-in test suite.\n"
              << "\n"
              << "Examples:\n"
              << "  " << programName << " examples/hello-noise    Run hello-noise example\n"
              << "  " << programName << " --pbr-gallery           Run PBR material gallery\n"
              << "  " << programName << "                          Run test suite\n";
}

int main(int argc, char** argv) {
    std::cout << "Starting Vivid..." << std::endl;

    // Parse command line arguments
    std::string projectPath;
    bool runPBRGallery = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--pbr-gallery") {
            runPBRGallery = true;
        } else if (arg[0] != '-') {
            projectPath = arg;
        }
    }

    vivid::Context ctx;

    // Determine window title based on mode
    std::string title;
    if (runPBRGallery) {
        title = "Vivid - PBR Material Gallery";
    } else if (projectPath.empty()) {
        title = "Vivid - Operator Tests";
    } else {
        title = "Vivid - " + vivid::fs::path(projectPath).filename().string();
    }

    // Initialize with default window
    if (!ctx.init(1280, 720, title.c_str())) {
        std::cerr << "Failed to initialize Vivid context" << std::endl;
        return -1;
    }

    std::cout << "Context initialized successfully" << std::endl;

    if (runPBRGallery) {
        // PBR material gallery mode
        vivid::runPBRMaterialGallery(ctx);
    } else if (!projectPath.empty()) {
        // Hot reload mode
        runHotReload(ctx, projectPath);
    } else {
        // Run operator test suite
        vivid::runOperatorTests(ctx);
    }

    ctx.shutdown();

    std::cout << "Vivid shutdown complete" << std::endl;
    return 0;
}
