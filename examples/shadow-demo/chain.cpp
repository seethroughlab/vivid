// Shadow Demo - Real-time shadow mapping with multiple light types
//
// This example demonstrates:
// - Directional light shadows (sun)
// - Shadow map rendering from light's perspective
// - Debug shadow map visualization
// - Light gizmo visualization (Unity-style)
//
// Controls:
// - 'L' to toggle light gizmos (directional=lines, spot=cone, point=sphere)
// - 'D' to toggle shadow map debug view
// - 'Space' to pause/resume animation
// - '1-4' to change shadow resolution (512/1024/2048/4096)

#include <vivid/vivid.h>
#include <iostream>

using namespace vivid;

// === SCENE STATE ===
static Mesh3D groundPlane;
static Mesh3D boxMesh;
static Mesh3D sphereMesh;
static Mesh3D torusMesh;
static Texture output;
static Texture shadowDebugOutput;

// Shadow settings (will be adjustable via keyboard)
static int shadowResolution = 2048;
static float shadowBias = 0.001f;
static float pcfRadius = 1.5f;
static bool pcfEnabled = true;
static bool showDebug = false;
static bool showLightGizmos = true;  // Toggle light visualization gizmos
static bool animateLights = true;

// === SETUP ===
void setup(Chain& chain) {
    chain.setOutput("out");
}

// === HELPER: Create scene geometry ===
void createGeometry(Context& ctx) {
    // Ground plane (20x20 units) - large floor
    groundPlane = ctx.createPlane(20.0f, 20.0f);

    // Box mesh for shadow casters
    boxMesh = ctx.createCube();

    // Sphere mesh
    sphereMesh = ctx.createSphere(0.5f, 32, 16);

    // Torus mesh
    torusMesh = ctx.createTorus(0.5f, 0.2f);
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

    // Light gizmos toggle
    if (ctx.wasKeyPressed(Key::L)) {
        showLightGizmos = !showLightGizmos;
    }

    // === TIME ===
    float t = animateLights ? ctx.time() : 0.0f;

    // === CAMERA ===
    // Fixed camera looking down at the scene from an angle
    float camAngle = t * 0.1f;  // Slower rotation
    float camDist = 12.0f;
    float camHeight = 8.0f;
    glm::vec3 camPos(
        std::cos(camAngle) * camDist,
        camHeight,
        std::sin(camAngle) * camDist
    );
    glm::vec3 camTarget(0.0f, 0.0f, 0.0f);  // Look at floor center

    Camera3D camera;
    camera.position = camPos;
    camera.target = camTarget;
    camera.fov = 45.0f;

    // === LIGHTS ===

    // Directional light (sun) - creates shadows
    Light sun = Light::directional(
        glm::vec3(-0.5f, -1.0f, -0.3f),  // direction
        glm::vec3(1.0f, 0.95f, 0.9f),    // warm white color
        1.0f                              // intensity
    );
    sun.castShadows = true;

    // Spot light (flashlight) - orbiting
    float spotAngle = t * 0.5f;
    glm::vec3 spotPos(
        std::cos(spotAngle) * 3.0f,
        4.0f,
        std::sin(spotAngle) * 3.0f
    );
    glm::vec3 spotDir = glm::normalize(-spotPos);
    Light flashlight = Light::spot(spotPos, spotDir, 15.0f, 25.0f,
                                   glm::vec3(1.0f, 0.9f, 0.7f), 2.0f);

    // Point light (lamp)
    Light lamp = Light::point(
        glm::vec3(-2.0f, 2.0f, 0.0f),
        glm::vec3(0.8f, 0.9f, 1.0f),
        1.5f,
        8.0f
    );

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

    // === SCENE LIGHTING ===
    SceneLighting lighting;
    lighting.ambientColor = glm::vec3(0.15f, 0.15f, 0.2f);
    lighting.ambientIntensity = 0.15f;  // Lower ambient = more visible shadows
    lighting.addLight(sun);
    // Note: Only sun casts shadows, other lights just add fill
    lighting.addLight(flashlight);
    lighting.addLight(lamp);

    // === BUILD MESH AND TRANSFORM LISTS (for shadow casters) ===
    std::vector<Mesh3D> meshes;
    std::vector<glm::mat4> transforms;

    // Ground plane transform (NOT added to shadow casters - it only receives shadows)
    glm::mat4 groundTransform = glm::mat4(1.0f);

    // Boxes at different positions
    glm::mat4 box1 = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.5f, 0.0f));
    glm::mat4 box2 = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.5f, 1.5f));
    box2 = glm::rotate(box2, t * 0.3f, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 box3 = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, -1.0f));
    box3 = glm::scale(box3, glm::vec3(0.7f));

    meshes.push_back(boxMesh);
    transforms.push_back(box1);
    meshes.push_back(boxMesh);
    transforms.push_back(box2);
    meshes.push_back(boxMesh);
    transforms.push_back(box3);

    // Spheres
    glm::mat4 sphere1 = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.5f, -0.5f));
    glm::mat4 sphere2 = glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, 1.2f, 1.5f));

    meshes.push_back(sphereMesh);
    transforms.push_back(sphere1);
    meshes.push_back(sphereMesh);
    transforms.push_back(sphere2);

    // Torus (rotating)
    glm::mat4 torusT = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    torusT = glm::rotate(torusT, t * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
    torusT = glm::rotate(torusT, glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    meshes.push_back(torusMesh);
    transforms.push_back(torusT);

    // === RENDER SHADOW MAP ===
    glm::vec3 sceneCenter(0.0f, 0.5f, 0.0f);
    float sceneRadius = 6.0f;

    Texture shadowMap = ctx.renderShadowMap(sun, meshes, transforms,
                                             sceneCenter, sceneRadius, shadowResolution);

    // === RENDER SCENE WITH SHADOWS ===
    // Render each mesh with PBR material and shadow mapping
    // First render clears, subsequent renders don't
    bool firstRender = true;

    // Ground
    ctx.render3DWithShadow(groundPlane, camera, groundTransform, groundMat, lighting, shadowMap, output,
                           firstRender ? glm::vec4(0.1f, 0.1f, 0.15f, 1.0f) : glm::vec4(0, 0, 0, -1));
    firstRender = false;

    // Boxes
    ctx.render3DWithShadow(boxMesh, camera, box1, boxMat, lighting, shadowMap, output, glm::vec4(0, 0, 0, -1));
    ctx.render3DWithShadow(boxMesh, camera, box2, boxMat, lighting, shadowMap, output, glm::vec4(0, 0, 0, -1));
    ctx.render3DWithShadow(boxMesh, camera, box3, boxMat, lighting, shadowMap, output, glm::vec4(0, 0, 0, -1));

    // Spheres
    ctx.render3DWithShadow(sphereMesh, camera, sphere1, sphereMat, lighting, shadowMap, output, glm::vec4(0, 0, 0, -1));
    ctx.render3DWithShadow(sphereMesh, camera, sphere2, sphereMat, lighting, shadowMap, output, glm::vec4(0, 0, 0, -1));

    // Torus
    ctx.render3DWithShadow(torusMesh, camera, torusT, torusMat, lighting, shadowMap, output, glm::vec4(0, 0, 0, -1));

    // === LIGHT GIZMOS ===
    // Unity-style visualization: directional=parallel lines, spot=cone, point=sphere
    if (showLightGizmos) {
        ctx.drawLightGizmos(lighting, camera, output);
    }

    // === DEBUG OVERLAY ===
    if (showDebug && shadowMap.valid()) {
        // Visualize shadow map - this overwrites the output with the depth view
        ctx.debugVisualizeShadowMap(shadowMap, shadowDebugOutput);
        // When debug mode is on, show the shadow map instead of the scene
        ctx.setOutput("out", shadowDebugOutput);
    } else {
        // Normal mode - show the rendered scene
        ctx.setOutput("out", output);
    }
}

VIVID_CHAIN(setup, update)
