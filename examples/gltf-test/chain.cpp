// Vivid Example: GLTF Model Gallery
// Demonstrates loading and rendering glTF models with PBR materials
// Drag mouse to rotate camera, scroll to zoom
// Press SPACE to cycle through models

#include <vivid/vivid.h>
#include <vivid/operators.h>
#include <iostream>

using namespace vivid;

// Static state that persists across frames
static std::unique_ptr<GLTFViewer> gltfViewer;
static bool initialized = false;

// Mouse interaction state
static glm::vec2 lastMousePos{0.0f, 0.0f};
static bool isDragging = false;

void setup(Context& ctx) {
    std::cout << "[GLTF Gallery] Setup - initializing..." << std::endl;

    // Create GLTFViewer - it renders directly to the swap chain
    gltfViewer = std::make_unique<GLTFViewer>();

    // Initialize
    gltfViewer->init(ctx);
    if (!gltfViewer->isInitialized()) {
        std::cerr << "[GLTF Gallery] Failed to initialize GLTFViewer!" << std::endl;
        return;
    }

    // Asset path (in app bundle)
    std::string assetPath = "build/runtime/vivid.app/Contents/MacOS/assets/";

    // Load HDR environment for IBL reflections
    std::string hdrPath = assetPath + "hdris/bryanston_park_sunrise_4k.hdr";
    if (gltfViewer->loadEnvironment(ctx, hdrPath)) {
        std::cout << "[GLTF Gallery] IBL environment loaded" << std::endl;
    } else {
        std::cout << "[GLTF Gallery] No IBL environment (using direct lighting only)" << std::endl;
    }

    // Load sample models from glTF-Sample-Models submodule
    std::string modelBasePath = "external/glTF-Sample-Models/2.0/";
    std::vector<std::string> modelPaths = {
        modelBasePath + "DamagedHelmet/glTF-Binary/DamagedHelmet.glb",
        modelBasePath + "Avocado/glTF-Binary/Avocado.glb",
        modelBasePath + "SciFiHelmet/glTF/SciFiHelmet.gltf",
        modelBasePath + "BoomBox/glTF-Binary/BoomBox.glb",
        modelBasePath + "AntiqueCamera/glTF-Binary/AntiqueCamera.glb",
    };

    int loaded = 0;
    for (const auto& path : modelPaths) {
        if (gltfViewer->loadModel(ctx, path) >= 0) {
            loaded++;
        }
    }

    if (loaded == 0) {
        std::cerr << "[GLTF Gallery] No models could be loaded!" << std::endl;
        return;
    }

    std::cout << "[GLTF Gallery] Loaded " << loaded << " models" << std::endl;

    // Setup initial view
    gltfViewer->camera().setOrbit(glm::vec3(0, 0, 0), 3.0f, 45.0f, 20.0f);
    gltfViewer->backgroundColor(0.1f, 0.1f, 0.15f);
    gltfViewer->lightDirection(0.5f, 0.6f, -0.2f);
    gltfViewer->lightIntensity(3.0f);

    initialized = true;

    std::cout << "[GLTF Gallery] Ready!" << std::endl;
    std::cout << "  Drag mouse to rotate camera" << std::endl;
    std::cout << "  Press SPACE to cycle through models" << std::endl;
    std::cout << "  Press ESC to exit" << std::endl;
    std::cout << "\nShowing: " << gltfViewer->modelName(0) << std::endl;
}

void update(Context& ctx) {
    if (!initialized) return;

    // Check for spacebar press to cycle models
    // GLFW_KEY_SPACE = 32
    if (ctx.wasKeyPressed(32)) {
        gltfViewer->nextModel();
        int idx = gltfViewer->currentModel();
        std::cout << "Showing: " << gltfViewer->modelName(idx)
                  << " (" << (idx + 1) << "/" << gltfViewer->modelCount() << ")" << std::endl;
    }

    // Mouse-controlled camera rotation
    // GLFW_MOUSE_BUTTON_LEFT = 0
    glm::vec2 mousePos = ctx.mousePosition();

    if (ctx.isMouseDown(0)) {
        if (isDragging) {
            // Calculate delta
            glm::vec2 delta = mousePos - lastMousePos;

            // Rotate camera based on mouse movement
            // Sensitivity factor
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
        float zoomFactor = 1.0f - scroll.y * 0.1f;
        gltfViewer->camera().orbitZoom(zoomFactor);
    }

    // Render - GLTFViewer renders directly to the swap chain
    gltfViewer->process(ctx);
}

VIVID_CHAIN(setup, update)
