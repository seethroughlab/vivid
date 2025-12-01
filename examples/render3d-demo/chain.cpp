// Render3D Demo - 3D skull model with PBR materials and lighting
// Features: OBJ model loading, texture mapping, orbit camera
// Controls: Mouse drag to orbit, scroll to zoom

#include <vivid/vivid.h>
#include <vivid/models/model_loader.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>

using namespace vivid;

// Scene objects
static Mesh3D skull;
static Mesh3D sphere;
static Camera3D camera;
static Texture output;

// Texture and material
static Texture skullTexture;
static TexturedPBRMaterial skullMaterial;

// Lighting
static SceneLighting lighting;
static Environment iblEnvironment;
static bool hasIBL = false;

// Camera control
static float cameraYaw = 0.0f;
static float cameraPitch = 0.2f;
static float cameraDistance = 15.0f;
static float lastMouseX = 0;
static float lastMouseY = 0;
static bool isDragging = false;

void setup(Chain& chain) {
    chain.setOutput("out");
}

void updateCamera() {
    float x = std::cos(cameraYaw) * std::cos(cameraPitch) * cameraDistance;
    float y = std::sin(cameraPitch) * cameraDistance;
    float z = std::sin(cameraYaw) * std::cos(cameraPitch) * cameraDistance;

    camera.position = glm::vec3(x, y, z);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
}

void update(Chain& chain, Context& ctx) {
    // Initialize on first frame
    if (!output.valid()) {
        output = ctx.createTexture();
        sphere = ctx.createSphere(0.5f, 32, 16);

        // Setup camera
        camera.fov = 45.0f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 100.0f;
        updateCamera();

        // Setup lighting - studio 3-point setup
        lighting.ambientColor = glm::vec3(0.15f);
        lighting.ambientIntensity = 0.3f;

        // Key light (warm, from upper left)
        lighting.addLight(Light::directional(
            glm::vec3(-0.5f, -1.0f, -0.5f),
            glm::vec3(1.0f, 0.95f, 0.9f),
            1.2f
        ));

        // Fill light (cool, from right)
        lighting.addLight(Light::directional(
            glm::vec3(0.8f, -0.3f, 0.5f),
            glm::vec3(0.7f, 0.8f, 1.0f),
            0.5f
        ));

        // Rim light (from behind)
        lighting.addLight(Light::directional(
            glm::vec3(0.0f, -0.2f, 1.0f),
            glm::vec3(1.0f, 1.0f, 1.0f),
            0.6f
        ));

        // Load skull model
        std::cout << "[render3d-demo] Loading skull model...\n";
        auto parsed = models::parseModel(ctx.projectPath() + "/assets/12140_Skull_v3_L2.obj");
        if (parsed.valid()) {
            skull = ctx.createMesh(parsed.vertices, parsed.indices);
            std::cout << "[render3d-demo] Skull loaded: " << parsed.vertices.size() << " vertices\n";
        } else {
            std::cout << "[render3d-demo] Failed to load skull model\n";
        }

        // Load skull texture
        std::cout << "[render3d-demo] Loading skull texture...\n";
        skullTexture = ctx.loadImageAsTexture(ctx.projectPath() + "/assets/Skull.jpg");
        if (skullTexture.valid()) {
            std::cout << "[render3d-demo] Texture loaded successfully\n";
            skullMaterial.albedoMap = &skullTexture;
        } else {
            std::cout << "[render3d-demo] Failed to load skull texture\n";
        }

        // Setup material - bone-like appearance
        skullMaterial.albedo = glm::vec3(1.0f);  // No tint, use texture color
        skullMaterial.metallic = 0.0f;           // Bone is not metallic
        skullMaterial.roughness = 0.7f;          // Fairly rough surface
        skullMaterial.ao = 1.0f;
        skullMaterial.normalStrength = 1.0f;

        // Try to load IBL environment (optional, for better reflections)
        // Place an HDR file in the assets folder for best results
        iblEnvironment = ctx.loadEnvironment(ctx.projectPath() + "/assets/environment.hdr");
        if (!iblEnvironment.valid()) {
            iblEnvironment = ctx.loadEnvironment("813-hdri-skies-com.hdr");
        }
        hasIBL = iblEnvironment.valid();
        if (hasIBL) {
            std::cout << "[render3d-demo] IBL environment loaded\n";
        }

        std::cout << "\n=== Render3D Demo ===\n";
        std::cout << "Drag mouse to orbit camera\n";
        std::cout << "Scroll to zoom\n\n";
    }

    // Camera orbit via mouse drag
    float mouseX = ctx.mouseX();
    float mouseY = ctx.mouseY();

    if (ctx.isMouseDown(0)) {
        if (!isDragging) {
            isDragging = true;
            lastMouseX = mouseX;
            lastMouseY = mouseY;
        } else {
            float dx = (mouseX - lastMouseX) * 0.01f;
            float dy = (mouseY - lastMouseY) * 0.01f;
            cameraYaw += dx;
            cameraPitch = glm::clamp(cameraPitch + dy, -1.4f, 1.4f);
            updateCamera();
            lastMouseX = mouseX;
            lastMouseY = mouseY;
        }
    } else {
        isDragging = false;
    }

    // Zoom via scroll
    float scroll = ctx.scrollDeltaY();
    if (scroll != 0) {
        cameraDistance = glm::clamp(cameraDistance - scroll * 1.0f, 5.0f, 30.0f);
        updateCamera();
    }

    // Animation
    float t = ctx.time();

    // Render gradient background (dark vignette effect)
    Context::ShaderParams bgParams;
    ctx.runShader(ctx.projectPath() + "/gradient.wgsl", nullptr, output, bgParams);

    // Use negative alpha to signal "don't clear" - render 3D on top of gradient
    glm::vec4 noClear(0.0f, 0.0f, 0.0f, -1.0f);

    // Render skull - slowly rotating
    if (skull.valid()) {
        glm::mat4 skullTransform = glm::mat4(1.0f);
        // The skull model is oriented with Y-up, rotate around Y for turntable
        skullTransform = glm::rotate(skullTransform, t * 0.3f, glm::vec3(0, 1, 0));
        // Rotate to face camera (model may be facing wrong way)
        skullTransform = glm::rotate(skullTransform, glm::radians(-90.0f), glm::vec3(1, 0, 0));
        // Scale if needed
        skullTransform = glm::scale(skullTransform, glm::vec3(0.1f));

        if (hasIBL && skullTexture.valid()) {
            // Use textured PBR with IBL
            ctx.render3DPBR(skull, camera, skullTransform, skullMaterial, lighting, iblEnvironment, output, noClear);
        } else {
            // Fallback to non-textured PBR
            PBRMaterial fallbackMat;
            fallbackMat.albedo = glm::vec3(0.9f, 0.85f, 0.75f);  // Bone color
            fallbackMat.roughness = 0.7f;
            fallbackMat.metallic = 0.0f;
            ctx.render3DPBR(skull, camera, skullTransform, fallbackMat, lighting, output, noClear);
        }
    } else {
        // Fallback: render a sphere if skull failed to load
        glm::mat4 sphereTransform = glm::mat4(1.0f);
        sphereTransform = glm::rotate(sphereTransform, t * 0.5f, glm::vec3(0, 1, 0));
        sphereTransform = glm::scale(sphereTransform, glm::vec3(2.0f));

        PBRMaterial fallback;
        fallback.albedo = glm::vec3(0.9f, 0.85f, 0.8f);
        fallback.roughness = 0.5f;
        fallback.metallic = 0.0f;

        ctx.render3DPBR(sphere, camera, sphereTransform, fallback, lighting, output, noClear);
    }

    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
