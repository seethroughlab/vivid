// Vivid Example: Multi-Light Demo
// Demonstrates the multi-light system with directional, point, and spot lights
// Press 1-4 to cycle through lighting presets
// Drag mouse to rotate camera, scroll to zoom

#include <vivid/vivid.h>
#include <vivid/operators.h>
#include <iostream>

using namespace vivid;

// Static state
static std::unique_ptr<GLTFViewer> gltfViewer;
static bool initialized = false;
static int currentPreset = 0;

// Mouse interaction state
static glm::vec2 lastMousePos{0.0f, 0.0f};
static bool isDragging = false;

// Light preset names
static const char* presetNames[] = {
    "Single Directional (Classic)",
    "Three-Point Lighting (Studio)",
    "Colored Point Lights (RGB)",
    "Animated Spot Light"
};

void setupPreset0(GLTFViewer& viewer) {
    // Classic single directional light
    viewer.clearLights();
    viewer.addLight(Light::directional(
        glm::vec3(0.5f, 0.6f, -0.2f),  // direction
        3.0f,                           // intensity
        glm::vec3(1.0f, 0.98f, 0.95f)  // warm white
    ));
}

void setupPreset1(GLTFViewer& viewer) {
    // Classic three-point lighting setup (studio lighting)
    viewer.clearLights();

    // Key light - main light, brightest, from front-right above
    viewer.addLight(Light::directional(
        glm::vec3(1.0f, 1.0f, 0.5f),
        4.0f,
        glm::vec3(1.0f, 1.0f, 1.0f)
    ));

    // Fill light - softer, from front-left, fills shadows
    viewer.addLight(Light::directional(
        glm::vec3(-1.0f, 0.5f, 0.5f),
        1.5f,
        glm::vec3(0.9f, 0.95f, 1.0f)  // slightly cool
    ));

    // Rim/back light - from behind, creates edge highlight
    viewer.addLight(Light::directional(
        glm::vec3(0.0f, 0.3f, -1.0f),
        2.0f,
        glm::vec3(1.0f, 0.98f, 0.9f)  // warm
    ));
}

void setupPreset2(GLTFViewer& viewer) {
    // Colored point lights (RGB)
    viewer.clearLights();

    // Red point light - left
    viewer.addLight(Light::point(
        glm::vec3(-3.0f, 1.0f, 2.0f),  // position
        150.0f,                         // intensity (lumens)
        10.0f,                          // range
        glm::vec3(1.0f, 0.2f, 0.1f)    // red
    ));

    // Green point light - right
    viewer.addLight(Light::point(
        glm::vec3(3.0f, 1.0f, 2.0f),
        150.0f,
        10.0f,
        glm::vec3(0.1f, 1.0f, 0.2f)  // green
    ));

    // Blue point light - top
    viewer.addLight(Light::point(
        glm::vec3(0.0f, 4.0f, 1.0f),
        150.0f,
        10.0f,
        glm::vec3(0.2f, 0.3f, 1.0f)  // blue
    ));

    // Add a subtle fill directional to see the model
    viewer.addLight(Light::directional(
        glm::vec3(0.0f, -1.0f, 0.0f),
        0.3f,
        glm::vec3(1.0f)
    ));
}

void setupPreset3(GLTFViewer& viewer) {
    // Animated spot light - setup once, animated in update
    viewer.clearLights();

    // Main spot light (will be animated)
    viewer.addLight(Light::spot(
        glm::vec3(0.0f, 3.0f, 3.0f),   // position
        glm::vec3(0.0f, -0.5f, -1.0f), // direction
        300.0f,                         // intensity
        0.2f,                           // inner cone (radians, ~11 degrees)
        0.5f,                           // outer cone (radians, ~29 degrees)
        15.0f,                          // range
        glm::vec3(1.0f, 0.95f, 0.8f)   // warm white
    ));

    // Ambient fill
    viewer.addLight(Light::directional(
        glm::vec3(0.0f, -1.0f, 0.0f),
        0.5f,
        glm::vec3(0.7f, 0.8f, 1.0f)  // cool ambient
    ));
}

void updateAnimatedLights(GLTFViewer& viewer, float time) {
    if (currentPreset == 3) {
        // Animate the spot light position in a circle
        float radius = 4.0f;
        float speed = 0.5f;
        float x = std::sin(time * speed) * radius;
        float z = std::cos(time * speed) * radius + 2.0f;

        Light spot = viewer.getLight(0);
        spot.position = glm::vec3(x, 3.0f, z);
        spot.direction = glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - spot.position);
        viewer.setLight(0, spot);
    }
}

void setup(Context& ctx) {
    std::cout << "[Lighting Demo] Setup - initializing..." << std::endl;

    gltfViewer = std::make_unique<GLTFViewer>();
    gltfViewer->init(ctx);

    if (!gltfViewer->isInitialized()) {
        std::cerr << "[Lighting Demo] Failed to initialize GLTFViewer!" << std::endl;
        return;
    }

    // Asset path
    std::string assetPath = "build/runtime/vivid.app/Contents/MacOS/assets/";

    // Load environment for IBL
    std::string hdrPath = assetPath + "hdris/bryanston_park_sunrise_4k.hdr";
    if (gltfViewer->loadEnvironment(ctx, hdrPath)) {
        std::cout << "[Lighting Demo] IBL environment loaded" << std::endl;
    }

    // Load a single model (DamagedHelmet is good for showing lighting)
    std::string modelPath = "external/glTF-Sample-Models/2.0/DamagedHelmet/glTF-Binary/DamagedHelmet.glb";
    if (gltfViewer->loadModel(ctx, modelPath) < 0) {
        std::cerr << "[Lighting Demo] Failed to load model!" << std::endl;
        return;
    }

    // Setup camera
    gltfViewer->camera().setOrbit(glm::vec3(0, 0, 0), 4.0f, 30.0f, 15.0f);
    gltfViewer->backgroundColor(0.05f, 0.05f, 0.08f);

    // Setup initial lighting preset
    setupPreset0(*gltfViewer);

    initialized = true;

    std::cout << "[Lighting Demo] Ready!" << std::endl;
    std::cout << "  Press 1-4 to switch lighting presets" << std::endl;
    std::cout << "  Press SPACE to cycle presets" << std::endl;
    std::cout << "  Drag mouse to rotate camera" << std::endl;
    std::cout << "\nCurrent: " << presetNames[0] << std::endl;
}

void update(Context& ctx) {
    if (!initialized) return;

    float currentTime = ctx.time();

    // Check for preset key presses
    // GLFW_KEY_1 = 49, GLFW_KEY_2 = 50, etc.
    int newPreset = -1;
    if (ctx.wasKeyPressed(49)) newPreset = 0;  // '1'
    if (ctx.wasKeyPressed(50)) newPreset = 1;  // '2'
    if (ctx.wasKeyPressed(51)) newPreset = 2;  // '3'
    if (ctx.wasKeyPressed(52)) newPreset = 3;  // '4'

    // SPACE (32) cycles through presets
    if (ctx.wasKeyPressed(32)) {
        newPreset = (currentPreset + 1) % 4;
    }

    if (newPreset >= 0 && newPreset != currentPreset) {
        currentPreset = newPreset;
        switch (currentPreset) {
            case 0: setupPreset0(*gltfViewer); break;
            case 1: setupPreset1(*gltfViewer); break;
            case 2: setupPreset2(*gltfViewer); break;
            case 3: setupPreset3(*gltfViewer); break;
        }
        std::cout << "Preset " << (currentPreset + 1) << ": "
                  << presetNames[currentPreset]
                  << " (" << gltfViewer->lightCount() << " lights)" << std::endl;
    }

    // Update animated lights
    updateAnimatedLights(*gltfViewer, currentTime);

    // Mouse-controlled camera rotation
    glm::vec2 mousePos = ctx.mousePosition();

    if (ctx.isMouseDown(0)) {
        if (isDragging) {
            glm::vec2 delta = mousePos - lastMousePos;
            float sensitivity = 0.3f;
            gltfViewer->camera().orbitRotate(delta.x * sensitivity, delta.y * sensitivity);
        }
        isDragging = true;
    } else {
        isDragging = false;
    }

    lastMousePos = mousePos;

    // Scroll wheel zoom
    glm::vec2 scroll = ctx.scrollDelta();
    if (std::abs(scroll.y) > 0.01f) {
        // Zoom in/out - negative scroll zooms out, positive zooms in
        float zoomFactor = 1.0f - scroll.y * 0.1f;
        gltfViewer->camera().orbitZoom(zoomFactor);
    }

    // Render
    gltfViewer->process(ctx);
}

VIVID_CHAIN(setup, update)
