// Shadow Demo - Real-time shadow mapping with multiple light types
//
// This example demonstrates:
// - Directional light shadows (sun)
// - Spot light shadows (flashlight)
// - Point light shadows (omnidirectional)
// - PCF soft shadows
//
// NOTE: Shadow mapping is not yet implemented in the Vivid runtime.
// This file serves as a template/specification for when shadows are added.

#include <vivid/vivid.h>

using namespace vivid;

// === SCENE STATE ===
static Mesh groundPlane;
static Mesh boxMesh;
static Mesh sphereMesh;
static Mesh torusMesh;
static Texture output;

// Shadow settings (will be adjustable via keyboard)
static int shadowResolution = 2048;
static float shadowBias = 0.001f;
static float pcfRadius = 1.5f;
static bool pcfEnabled = true;
static bool showDebug = false;
static bool animateLights = true;

// === SETUP ===
void setup(Chain& chain) {
    chain.setResolution(1920, 1080);
    chain.setOutput("out");
}

// === HELPER: Create scene geometry ===
void createGeometry(Context& ctx) {
    // Ground plane (10x10 units)
    // GOAL: Large flat surface to receive shadows
    groundPlane = ctx.createPlane(10.0f, 10.0f, 1, 1);

    // Box mesh for shadow casters
    boxMesh = ctx.createBox(1.0f);

    // Sphere mesh
    sphereMesh = ctx.createSphere(0.5f, 32, 16);

    // Torus mesh
    torusMesh = ctx.createTorus(0.5f, 0.2f, 32, 16);
}

// === UPDATE ===
void update(Chain& chain, Context& ctx) {
    // Create geometry on first frame
    static bool initialized = false;
    if (!initialized) {
        createGeometry(ctx);
        output = ctx.createTexture();
        initialized = true;
    }

    // === KEYBOARD CONTROLS ===
    // Shadow quality
    if (ctx.wasKeyPressed(Key::Num1)) shadowResolution = 512;
    if (ctx.wasKeyPressed(Key::Num2)) shadowResolution = 1024;
    if (ctx.wasKeyPressed(Key::Num3)) shadowResolution = 2048;
    if (ctx.wasKeyPressed(Key::Num4)) shadowResolution = 4096;

    // Bias adjustment
    if (ctx.wasKeyPressed(Key::B)) {
        if (ctx.isKeyDown(Key::LeftShift)) {
            shadowBias = std::max(0.0001f, shadowBias - 0.0002f);
        } else {
            shadowBias = std::min(0.01f, shadowBias + 0.0002f);
        }
    }

    // PCF toggle
    if (ctx.wasKeyPressed(Key::P)) {
        pcfEnabled = !pcfEnabled;
    }

    // Debug view toggle
    if (ctx.wasKeyPressed(Key::D)) {
        showDebug = !showDebug;
    }

    // Animation toggle
    if (ctx.wasKeyPressed(Key::Space)) {
        animateLights = !animateLights;
    }

    // === TIME ===
    float t = animateLights ? ctx.time() : 0.0f;

    // === CAMERA ===
    // Orbiting camera
    float camAngle = t * 0.2f;
    float camDist = 8.0f;
    float camHeight = 5.0f;
    glm::vec3 camPos(
        std::cos(camAngle) * camDist,
        camHeight,
        std::sin(camAngle) * camDist
    );
    glm::vec3 camTarget(0.0f, 0.5f, 0.0f);

    Camera3D camera;
    camera.position = camPos;
    camera.target = camTarget;
    camera.fov = 45.0f;

    // === LIGHTS ===

    // Directional light (sun)
    DirectionalLight sun;
    sun.direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
    sun.color = glm::vec3(1.0f, 0.95f, 0.9f);
    sun.intensity = 1.0f;
    // sun.castShadows = true;  // When implemented

    // Spot light (flashlight)
    float spotAngle = t * 0.5f;
    SpotLight flashlight;
    flashlight.position = glm::vec3(
        std::cos(spotAngle) * 3.0f,
        4.0f,
        std::sin(spotAngle) * 3.0f
    );
    flashlight.direction = glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - flashlight.position);
    flashlight.color = glm::vec3(1.0f, 0.9f, 0.7f);
    flashlight.intensity = 2.0f;
    flashlight.innerAngle = 15.0f;
    flashlight.outerAngle = 25.0f;
    // flashlight.castShadows = true;  // When implemented

    // Point light (lamp)
    PointLight lamp;
    lamp.position = glm::vec3(-2.0f, 2.0f, 0.0f);
    lamp.color = glm::vec3(0.8f, 0.9f, 1.0f);
    lamp.intensity = 1.5f;
    lamp.radius = 8.0f;
    // lamp.castShadows = true;  // When implemented

    // === MATERIALS ===

    // Ground material (gray, matte)
    PBRMaterial groundMat;
    groundMat.albedo = glm::vec3(0.3f, 0.3f, 0.35f);
    groundMat.roughness = 0.9f;
    groundMat.metallic = 0.0f;

    // Box material (red, semi-glossy)
    PBRMaterial boxMat;
    boxMat.albedo = glm::vec3(0.8f, 0.2f, 0.2f);
    boxMat.roughness = 0.4f;
    boxMat.metallic = 0.0f;

    // Sphere material (gold, metallic)
    PBRMaterial sphereMat;
    sphereMat.albedo = glm::vec3(1.0f, 0.85f, 0.4f);
    sphereMat.roughness = 0.3f;
    sphereMat.metallic = 1.0f;

    // Torus material (blue, glossy)
    PBRMaterial torusMat;
    torusMat.albedo = glm::vec3(0.2f, 0.4f, 0.9f);
    torusMat.roughness = 0.2f;
    torusMat.metallic = 0.5f;

    // === RENDER SCENE ===
    ctx.beginRender3D(output, camera);

    // GOAL: When shadow mapping is implemented, the render call would be:
    // ctx.setShadowSettings(shadowResolution, shadowBias, pcfRadius, pcfEnabled);

    // Add lights
    ctx.addLight(sun);
    ctx.addLight(flashlight);
    ctx.addLight(lamp);

    // Ground plane
    glm::mat4 groundTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
    groundTransform = glm::rotate(groundTransform, -glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
    ctx.render3DPBR(groundPlane, groundMat, groundTransform);

    // Boxes at different positions
    glm::mat4 box1 = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.5f, 0.0f));
    glm::mat4 box2 = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.5f, 1.5f));
    box2 = glm::rotate(box2, t * 0.3f, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 box3 = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, -1.0f));
    box3 = glm::scale(box3, glm::vec3(0.7f));

    ctx.render3DPBR(boxMesh, boxMat, box1);
    ctx.render3DPBR(boxMesh, boxMat, box2);
    ctx.render3DPBR(boxMesh, boxMat, box3);

    // Spheres
    glm::mat4 sphere1 = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.5f, -0.5f));
    glm::mat4 sphere2 = glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, 1.2f, 1.5f));

    ctx.render3DPBR(sphereMesh, sphereMat, sphere1);
    ctx.render3DPBR(sphereMesh, sphereMat, sphere2);

    // Torus (rotating)
    glm::mat4 torusT = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    torusT = glm::rotate(torusT, t * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
    torusT = glm::rotate(torusT, glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    ctx.render3DPBR(torusMesh, torusMat, torusT);

    ctx.endRender3D();

    // === DEBUG OVERLAY ===
    if (showDebug) {
        // GOAL: When implemented, show shadow map texture in corner
        // ctx.debugShowTexture(sunShadowMap, 0, 0, 256, 256);
    }

    // === OUTPUT ===
    chain.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
