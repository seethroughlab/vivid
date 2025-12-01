// Textured PBR Demo
// Demonstrates PBR rendering with texture maps
// Features: albedo, normal, roughness, and ambient occlusion maps
// Textures from Poly Haven (CC0 license)
// Press SPACE to toggle IBL on/off
// Mouse drag to orbit camera, scroll to zoom

#include <vivid/vivid.h>
#include <vivid/models/model_loader.h>
#include <iostream>
#include <cmath>

using namespace vivid;

// Scene objects
static Mesh3D cube;
static Mesh3D sphere;
static Mesh3D teapot;
static Camera3D camera;
static Texture output;

// Lighting
static SceneLighting lighting;
static Environment iblEnvironment;
static bool hasIBL = false;
static bool useIBL = true;

// Texture-loaded materials
static TexturedPBRMaterial brickMaterial;
static TexturedPBRMaterial metalMaterial;

// Textures (stored separately for lifetime management)
static Texture brickAlbedo;
static Texture brickNormal;
static Texture brickRoughness;
static Texture brickAO;
static Texture metalAlbedo;
static Texture metalNormal;
static Texture metalRoughness;
static Texture metalMetallic;

// Camera control
static float cameraYaw = 0.5f;
static float cameraPitch = 0.4f;
static float cameraDistance = 5.0f;
static float lastMouseX = 0;
static float lastMouseY = 0;
static bool isDragging = false;

void setup(Chain& chain) {
    chain.setOutput("out");
}

void updateCamera() {
    // Orbit camera around origin
    float x = std::cos(cameraYaw) * std::cos(cameraPitch) * cameraDistance;
    float y = std::sin(cameraPitch) * cameraDistance;
    float z = std::sin(cameraYaw) * std::cos(cameraPitch) * cameraDistance;

    camera.position = glm::vec3(x, y, z);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
}

void update(Chain& chain, Context& ctx) {
    // Initialize on first frame
    if (!cube.valid()) {
        cube = ctx.createCube();
        sphere = ctx.createSphere(0.5f, 48, 32);
        output = ctx.createTexture();

        // Load teapot model using the models addon
        auto parsed = models::parseModel(ctx.projectPath() + "/teapot.obj");
        if (parsed.valid()) {
            teapot = ctx.createMesh(parsed.vertices, parsed.indices);
            std::cout << "Teapot loaded successfully (" << parsed.vertices.size() << " vertices)\n";
        } else {
            std::cout << "Warning: Could not load teapot.obj\n";
        }

        // Setup camera
        camera.fov = 45.0f;
        updateCamera();

        // Setup lighting - 3-point setup
        lighting.ambientColor = glm::vec3(0.02f, 0.02f, 0.03f);
        lighting.ambientIntensity = 0.2f;

        // Key light (warm)
        lighting.addLight(Light::directional(
            glm::vec3(-0.5f, -1.0f, -0.3f),
            glm::vec3(1.0f, 0.95f, 0.85f),
            1.0f
        ));

        // Fill light (cool)
        lighting.addLight(Light::directional(
            glm::vec3(0.8f, -0.3f, 0.5f),
            glm::vec3(0.5f, 0.6f, 0.9f),
            0.4f
        ));

        // === Load Brick Textures ===
        std::cout << "Loading brick textures...\n";
        brickAlbedo = ctx.loadImageAsTexture("textures/brick/brick_wall_003_diffuse_1k.jpg");
        brickNormal = ctx.loadImageAsTexture("textures/brick/brick_wall_003_nor_gl_1k.jpg");
        brickRoughness = ctx.loadImageAsTexture("textures/brick/brick_wall_003_rough_1k.jpg");
        brickAO = ctx.loadImageAsTexture("textures/brick/brick_wall_003_ao_1k.jpg");

        brickMaterial.albedo = glm::vec3(1.0f);  // No tint
        brickMaterial.metallic = 0.0f;
        brickMaterial.roughness = 1.0f;  // Will be multiplied by texture
        brickMaterial.ao = 1.0f;
        brickMaterial.normalStrength = 1.0f;

        if (brickAlbedo.valid()) {
            brickMaterial.albedoMap = &brickAlbedo;
            std::cout << "  - Albedo map loaded\n";
        }
        if (brickNormal.valid()) {
            brickMaterial.normalMap = &brickNormal;
            std::cout << "  - Normal map loaded\n";
        }
        if (brickRoughness.valid()) {
            brickMaterial.roughnessMap = &brickRoughness;
            std::cout << "  - Roughness map loaded\n";
        }
        if (brickAO.valid()) {
            brickMaterial.aoMap = &brickAO;
            std::cout << "  - AO map loaded\n";
        }

        // === Load Metal Textures ===
        std::cout << "Loading metal textures...\n";
        metalAlbedo = ctx.loadImageAsTexture("textures/metal/metal_plate_diff_1k.jpg");
        metalNormal = ctx.loadImageAsTexture("textures/metal/metal_plate_nor_gl_1k.jpg");
        metalRoughness = ctx.loadImageAsTexture("textures/metal/metal_plate_rough_1k.jpg");
        metalMetallic = ctx.loadImageAsTexture("textures/metal/metal_plate_metal_1k.jpg");

        metalMaterial.albedo = glm::vec3(1.0f);
        metalMaterial.metallic = 1.0f;  // Will be multiplied by texture
        metalMaterial.roughness = 1.0f;  // Will be multiplied by texture
        metalMaterial.ao = 1.0f;
        metalMaterial.normalStrength = 1.0f;

        if (metalAlbedo.valid()) {
            metalMaterial.albedoMap = &metalAlbedo;
            std::cout << "  - Albedo map loaded\n";
        }
        if (metalNormal.valid()) {
            metalMaterial.normalMap = &metalNormal;
            std::cout << "  - Normal map loaded\n";
        }
        if (metalRoughness.valid()) {
            metalMaterial.roughnessMap = &metalRoughness;
            std::cout << "  - Roughness map loaded\n";
        }
        if (metalMetallic.valid()) {
            metalMaterial.metallicMap = &metalMetallic;
            std::cout << "  - Metallic map loaded\n";
        }

        // Try to load IBL environment
        iblEnvironment = ctx.loadEnvironment("813-hdri-skies-com.hdr");
        if (iblEnvironment.valid()) {
            hasIBL = true;
            std::cout << "\nIBL environment loaded!\n";
        } else {
            std::cout << "\nNote: Place an HDR file named 'environment.hdr' in the example folder for IBL\n";
        }

        std::cout << "\n=== Textured PBR Demo ===\n";
        std::cout << "Textures from Poly Haven (CC0 license)\n";
        std::cout << "Press SPACE to toggle IBL " << (hasIBL ? "(available)" : "(not loaded)") << "\n";
        std::cout << "Drag mouse to orbit camera\n";
        std::cout << "Scroll to zoom\n\n";
    }

    // Handle input
    if (ctx.wasKeyPressed(Key::Space) && hasIBL) {
        useIBL = !useIBL;
        std::cout << "IBL: " << (useIBL ? "ON" : "OFF") << "\n";
    }

    // Camera orbit
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

    // Zoom
    float scroll = ctx.scrollDeltaY();
    if (scroll != 0) {
        cameraDistance = glm::clamp(cameraDistance - scroll * 0.5f, 2.0f, 15.0f);
        updateCamera();
    }

    // Animation
    float t = ctx.time();

    // Clear and set background
    glm::vec4 clearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glm::vec4 noClear(0.0f, 0.0f, 0.0f, -1.0f);

    // === Render Scene ===

    // Metal cube - rotating
    glm::mat4 cubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.2f, 0.0f, 0.0f));
    cubeTransform = glm::rotate(cubeTransform, t * 0.3f, glm::vec3(0, 1, 0));
    cubeTransform = glm::rotate(cubeTransform, t * 0.2f, glm::vec3(1, 0, 0));

    if (hasIBL && useIBL) {
        ctx.render3DPBR(cube, camera, cubeTransform, metalMaterial, lighting, iblEnvironment, output, clearColor);
    } else {
        PBRMaterial fallbackMetal;
        fallbackMetal.albedo = glm::vec3(0.8f, 0.8f, 0.9f);
        fallbackMetal.roughness = 0.3f;
        fallbackMetal.metallic = 1.0f;
        ctx.render3DPBR(cube, camera, cubeTransform, fallbackMetal, lighting, output, clearColor);
    }

    // Brick sphere
    glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.2f, 0.0f, 0.0f));
    sphereTransform = glm::rotate(sphereTransform, t * 0.2f, glm::vec3(0, 1, 0));

    if (hasIBL && useIBL) {
        ctx.render3DPBR(sphere, camera, sphereTransform, brickMaterial, lighting, iblEnvironment, output, noClear);
    } else {
        PBRMaterial fallbackBrick;
        fallbackBrick.albedo = glm::vec3(0.6f, 0.3f, 0.2f);
        fallbackBrick.roughness = 0.8f;
        fallbackBrick.metallic = 0.0f;
        ctx.render3DPBR(sphere, camera, sphereTransform, fallbackBrick, lighting, output, noClear);
    }

    // Metal sphere - hovering
    float hoverY = 0.5f + std::sin(t * 1.5f) * 0.2f;
    glm::mat4 metalSphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, hoverY, 1.0f));

    if (hasIBL && useIBL) {
        ctx.render3DPBR(sphere, camera, metalSphereTransform, metalMaterial, lighting, iblEnvironment, output, noClear);
    } else {
        PBRMaterial fallbackMetal;
        fallbackMetal.albedo = glm::vec3(0.8f, 0.8f, 0.9f);
        fallbackMetal.roughness = 0.3f;
        fallbackMetal.metallic = 1.0f;
        ctx.render3DPBR(sphere, camera, metalSphereTransform, fallbackMetal, lighting, output, noClear);
    }

    // Teapot - center, slowly rotating
    if (teapot.valid()) {
        glm::mat4 teapotTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.3f, -1.0f));
        teapotTransform = glm::rotate(teapotTransform, t * 0.5f, glm::vec3(0, 1, 0));
        teapotTransform = glm::scale(teapotTransform, glm::vec3(0.5f));  // Scale down if needed

        if (hasIBL && useIBL) {
            ctx.render3DPBR(teapot, camera, teapotTransform, metalMaterial, lighting, iblEnvironment, output, noClear);
        } else {
            PBRMaterial fallbackMetal;
            fallbackMetal.albedo = glm::vec3(0.8f, 0.8f, 0.9f);
            fallbackMetal.roughness = 0.3f;
            fallbackMetal.metallic = 1.0f;
            ctx.render3DPBR(teapot, camera, teapotTransform, fallbackMetal, lighting, output, noClear);
        }
    }

    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
