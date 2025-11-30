// 3D Lighting Demo
// Demonstrates Phong, PBR, and PBR+IBL rendering with multiple lights
// Press SPACE to cycle between shading modes
// Mouse drag to orbit camera, scroll to zoom

#include <vivid/vivid.h>
#include <iostream>

using namespace vivid;

// Scene state
static Mesh3D sphere;
static Mesh3D groundPlane;
static Camera3D camera;
static Texture output;

// Lighting
static SceneLighting lighting;

// IBL Environment (optional)
static Environment iblEnvironment;
static bool hasIBL = false;

// Materials - multiple for variety
static PhongMaterial phongRed;
static PhongMaterial phongGreen;
static PhongMaterial phongBlue;
static PhongMaterial phongWhite;

static PBRMaterial pbrRough;    // Rough plastic
static PBRMaterial pbrShiny;    // Shiny plastic
static PBRMaterial pbrMetal;    // Metal
static PBRMaterial pbrGround;   // Ground

// Mode: 0 = Phong, 1 = PBR, 2 = PBR+IBL
static int renderMode = 0;  // Start with Phong to see shading
static int maxMode = 1;     // Increases to 2 if IBL environment loads

// Camera control
static float lastMouseX = 0;
static float lastMouseY = 0;
static bool isDragging = false;

void setup(Chain& chain) {
    // Register the output texture with the chain
    chain.setOutput("out");
}

void update(Chain& chain, Context& ctx) {
    // Create meshes on first frame
    if (!sphere.valid()) {
        sphere = ctx.createSphere(0.4f, 32, 24);
        groundPlane = ctx.createPlane(6.0f, 6.0f);
        output = ctx.createTexture();

        // Setup camera - looking down at the scene
        camera.position = glm::vec3(4.0f, 3.5f, 4.0f);
        camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
        camera.fov = 45.0f;

        // Setup lighting - key light + fill + back + animated point
        lighting.ambientColor = glm::vec3(0.15f, 0.15f, 0.2f);
        lighting.ambientIntensity = 0.4f;

        // Key light (bright, warm, from upper right)
        lighting.addLight(Light::directional(
            glm::vec3(-0.5f, -1.0f, -0.3f),  // direction
            glm::vec3(1.0f, 0.95f, 0.8f),     // warm white
            1.2f                              // intensity
        ));

        // Fill light (dimmer, cool, from left)
        lighting.addLight(Light::directional(
            glm::vec3(0.8f, -0.5f, 0.2f),
            glm::vec3(0.6f, 0.7f, 1.0f),  // cool blue
            0.4f
        ));

        // Back/rim light
        lighting.addLight(Light::directional(
            glm::vec3(0.0f, -0.3f, 1.0f),
            glm::vec3(1.0f, 1.0f, 1.0f),
            0.3f
        ));

        // Animated point light (warm orange, orbiting)
        lighting.addLight(Light::point(
            glm::vec3(2, 1, 0),
            glm::vec3(1.0f, 0.6f, 0.2f),  // orange
            1.5f,  // intensity
            5.0f   // radius
        ));

        // === Phong Materials ===
        // Red - shiny
        phongRed.ambient = glm::vec3(0.1f, 0.02f, 0.02f);
        phongRed.diffuse = glm::vec3(0.9f, 0.2f, 0.2f);
        phongRed.specular = glm::vec3(1.0f, 0.8f, 0.8f);
        phongRed.shininess = 64.0f;

        // Green - matte
        phongGreen.ambient = glm::vec3(0.02f, 0.1f, 0.02f);
        phongGreen.diffuse = glm::vec3(0.2f, 0.8f, 0.3f);
        phongGreen.specular = glm::vec3(0.3f, 0.5f, 0.3f);
        phongGreen.shininess = 8.0f;

        // Blue - very shiny
        phongBlue.ambient = glm::vec3(0.02f, 0.02f, 0.1f);
        phongBlue.diffuse = glm::vec3(0.2f, 0.3f, 0.9f);
        phongBlue.specular = glm::vec3(1.0f, 1.0f, 1.0f);
        phongBlue.shininess = 128.0f;

        // White ground
        phongWhite.ambient = glm::vec3(0.1f, 0.1f, 0.1f);
        phongWhite.diffuse = glm::vec3(0.7f, 0.7f, 0.7f);
        phongWhite.specular = glm::vec3(0.2f, 0.2f, 0.2f);
        phongWhite.shininess = 16.0f;

        // === PBR Materials ===
        // Rough red plastic
        pbrRough.albedo = glm::vec3(0.9f, 0.2f, 0.2f);
        pbrRough.metallic = 0.0f;
        pbrRough.roughness = 0.7f;
        pbrRough.ao = 1.0f;

        // Shiny green plastic
        pbrShiny.albedo = glm::vec3(0.2f, 0.8f, 0.3f);
        pbrShiny.metallic = 0.0f;
        pbrShiny.roughness = 0.2f;
        pbrShiny.ao = 1.0f;

        // Blue metal
        pbrMetal.albedo = glm::vec3(0.3f, 0.4f, 0.9f);
        pbrMetal.metallic = 0.9f;
        pbrMetal.roughness = 0.3f;
        pbrMetal.ao = 1.0f;

        // Ground - slightly rough
        pbrGround.albedo = glm::vec3(0.6f, 0.6f, 0.6f);
        pbrGround.metallic = 0.0f;
        pbrGround.roughness = 0.5f;
        pbrGround.ao = 1.0f;

        // Try to load IBL environment (optional)
        // Place an HDR file at examples/lighting-demo/environment.hdr to enable IBL
        iblEnvironment = ctx.loadEnvironment("environment.hdr");
        if (iblEnvironment.valid()) {
            hasIBL = true;
            maxMode = 2;
            std::cout << "IBL environment loaded!\n";
        }

        std::cout << "\n=== 3D Lighting Demo ===\n";
        std::cout << "Press SPACE to cycle shading modes\n";
        std::cout << "Drag mouse to orbit camera\n";
        std::cout << "Scroll to zoom\n";
        std::cout << "Modes: PHONG, PBR";
        if (hasIBL) std::cout << ", PBR+IBL";
        std::cout << "\nCurrently: PHONG mode\n\n";
    }

    // Handle input - cycle between shading modes with SPACE
    if (ctx.wasKeyPressed(Key::Space)) {
        renderMode = (renderMode + 1) % (maxMode + 1);
        const char* modeNames[] = { "PHONG", "PBR", "PBR+IBL" };
        std::cout << "Switched to " << modeNames[renderMode] << " shading\n";
    }

    // Camera orbit with mouse drag
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
            camera.orbit(dx, dy);
            lastMouseX = mouseX;
            lastMouseY = mouseY;
        }
    } else {
        isDragging = false;
    }

    // Zoom with scroll
    float scroll = ctx.scrollDeltaY();
    if (scroll != 0) {
        camera.zoom(scroll * 0.5f);
    }

    // Animation time
    float t = ctx.time();

    // Animate the point light (orbiting)
    if (lighting.lights.size() > 3) {
        lighting.lights[3].position = glm::vec3(
            std::cos(t * 0.8f) * 2.5f,
            1.2f + std::sin(t * 0.5f) * 0.3f,
            std::sin(t * 0.8f) * 2.5f
        );
    }

    // Sphere positions and animations
    float bounce1 = std::abs(std::sin(t * 2.0f)) * 0.2f;
    float bounce2 = std::abs(std::sin(t * 2.0f + 1.0f)) * 0.2f;
    float bounce3 = std::abs(std::sin(t * 2.0f + 2.0f)) * 0.2f;

    glm::mat4 sphere1Transform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.2f, 0.4f + bounce1, 0.0f));
    glm::mat4 sphere2Transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.4f + bounce2, 0.0f));
    glm::mat4 sphere3Transform = glm::translate(glm::mat4(1.0f), glm::vec3(1.2f, 0.4f + bounce3, 0.0f));

    // Ground plane - rotate to be horizontal (default is vertical)
    glm::mat4 groundTransform = glm::rotate(glm::mat4(1.0f), -glm::half_pi<float>(), glm::vec3(1, 0, 0));

    // Clear background color (dark gradient)
    glm::vec4 clearColor(0.05f, 0.05f, 0.1f, 1.0f);

    if (renderMode == 0) {
        // === PHONG RENDERING ===
        // Render ground first
        ctx.render3DPhong(groundPlane, camera, groundTransform, phongWhite, lighting, output, clearColor);

        // Render spheres (note: each call clears, so we need multi-object support)
        // For now, just render the middle sphere to show the lighting
        ctx.render3DPhong(sphere, camera, sphere2Transform, phongRed, lighting, output, clearColor);
    } else if (renderMode == 1) {
        // === PBR RENDERING ===
        ctx.render3DPBR(sphere, camera, sphere2Transform, pbrShiny, lighting, output, clearColor);
    } else {
        // === PBR + IBL RENDERING ===
        ctx.render3DPBR(sphere, camera, sphere2Transform, pbrMetal, lighting, iblEnvironment, output, clearColor);
    }

    // Set output for display
    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
