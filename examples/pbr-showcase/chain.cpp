// PBR + IBL Showcase Demo
// Demonstrates Image-Based Lighting with a material grid
// Shows roughness vs metallic variations, plus emissive objects
// Press SPACE to toggle IBL on/off
// Mouse drag to orbit camera, scroll to zoom

#include <vivid/vivid.h>
#include <iostream>
#include <cmath>

using namespace vivid;

// Scene objects
static Mesh3D sphere;
static Mesh3D torus;
static Mesh3D groundPlane;
static Mesh3D cylinder;
static Camera3D camera;
static Texture output;

// Lighting
static SceneLighting lighting;
static Environment iblEnvironment;
static bool hasIBL = false;
static bool useIBL = true;

// Material grid (5x5 = 25 spheres)
static const int GRID_SIZE = 5;
static PBRMaterial materials[GRID_SIZE][GRID_SIZE];
static PBRMaterial groundMaterial;
static PBRMaterial torusMaterial;
static PBRMaterial emissiveMaterial;

// Camera control
static float lastMouseX = 0;
static float lastMouseY = 0;
static bool isDragging = false;

void setup(Chain& chain) {
    chain.setOutput("out");
}

void update(Chain& chain, Context& ctx) {
    // Create meshes on first frame
    if (!sphere.valid()) {
        sphere = ctx.createSphere(0.35f, 32, 24);
        torus = ctx.createTorus(0.5f, 0.15f);
        groundPlane = ctx.createPlane(12.0f, 12.0f);
        cylinder = ctx.createCylinder(0.15f, 0.8f);
        output = ctx.createTexture();

        // Setup camera
        camera.position = glm::vec3(6.0f, 5.0f, 8.0f);
        camera.target = glm::vec3(0.0f, 0.5f, 0.0f);
        camera.fov = 45.0f;

        // Setup lighting - 3-point setup
        lighting.ambientColor = glm::vec3(0.02f, 0.02f, 0.03f);
        lighting.ambientIntensity = 0.1f;

        // Key light (warm sun)
        lighting.addLight(Light::directional(
            glm::vec3(-0.5f, -1.0f, -0.3f),
            glm::vec3(1.0f, 0.95f, 0.85f),
            0.8f
        ));

        // Fill light (cool sky)
        lighting.addLight(Light::directional(
            glm::vec3(0.8f, -0.3f, 0.5f),
            glm::vec3(0.5f, 0.6f, 0.9f),
            0.3f
        ));

        // Rim light
        lighting.addLight(Light::directional(
            glm::vec3(0.0f, -0.5f, 1.0f),
            glm::vec3(1.0f, 1.0f, 1.0f),
            0.2f
        ));

        // === Create material grid (roughness vs metallic) ===
        for (int r = 0; r < GRID_SIZE; r++) {
            for (int m = 0; m < GRID_SIZE; m++) {
                float roughness = r / float(GRID_SIZE - 1);
                float metallic = m / float(GRID_SIZE - 1);

                materials[r][m].albedo = glm::vec3(0.9f, 0.2f, 0.2f);  // Red base
                materials[r][m].roughness = glm::max(0.05f, roughness); // Clamp to avoid pure mirror
                materials[r][m].metallic = metallic;
                materials[r][m].ao = 1.0f;
            }
        }

        // Ground - slightly rough dark grey
        groundMaterial.albedo = glm::vec3(0.15f, 0.15f, 0.15f);
        groundMaterial.metallic = 0.0f;
        groundMaterial.roughness = 0.8f;
        groundMaterial.ao = 1.0f;

        // Torus - gold metal
        torusMaterial = PBRMaterial::gold();

        // Emissive pedestal material
        emissiveMaterial.albedo = glm::vec3(0.1f, 0.1f, 0.1f);
        emissiveMaterial.metallic = 0.0f;
        emissiveMaterial.roughness = 0.3f;
        emissiveMaterial.emissive = glm::vec3(0.2f, 0.5f, 1.0f);  // Blue glow

        // Try to load IBL environment
        iblEnvironment = ctx.loadEnvironment("environment.hdr");
        if (iblEnvironment.valid()) {
            hasIBL = true;
            std::cout << "IBL environment loaded!\n";
        } else {
            std::cout << "Note: Place an HDR file named 'environment.hdr' in the example folder for IBL\n";
        }

        std::cout << "\n=== PBR + IBL Showcase ===\n";
        std::cout << "Grid shows roughness (horizontal) vs metallic (vertical)\n";
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
            camera.orbit(dx, dy);
            lastMouseX = mouseX;
            lastMouseY = mouseY;
        }
    } else {
        isDragging = false;
    }

    // Zoom
    float scroll = ctx.scrollDeltaY();
    if (scroll != 0) {
        camera.zoom(scroll * 0.5f);
    }

    // Animation
    float t = ctx.time();

    // Clear and set background
    glm::vec4 clearColor(0.02f, 0.02f, 0.03f, 1.0f);
    // Negative alpha = don't clear, keep existing content
    glm::vec4 noClear(0.0f, 0.0f, 0.0f, -1.0f);

    // Ground plane transform
    glm::mat4 groundTransform = glm::rotate(glm::mat4(1.0f), -glm::half_pi<float>(), glm::vec3(1, 0, 0));

    // Render ground
    if (hasIBL && useIBL) {
        ctx.render3DPBR(groundPlane, camera, groundTransform, groundMaterial, lighting, iblEnvironment, output, clearColor);
    } else {
        ctx.render3DPBR(groundPlane, camera, groundTransform, groundMaterial, lighting, output, clearColor);
    }

    // Render material grid spheres
    float gridSpacing = 0.9f;
    float gridOffsetX = -((GRID_SIZE - 1) * gridSpacing) / 2.0f;
    float gridOffsetZ = -((GRID_SIZE - 1) * gridSpacing) / 2.0f;

    for (int r = 0; r < GRID_SIZE; r++) {
        for (int m = 0; m < GRID_SIZE; m++) {
            float x = gridOffsetX + r * gridSpacing;
            float z = gridOffsetZ + m * gridSpacing;
            float y = 0.4f;  // Slightly above ground

            glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));

            if (hasIBL && useIBL) {
                ctx.render3DPBR(sphere, camera, sphereTransform, materials[r][m], lighting, iblEnvironment, output, noClear);
            } else {
                ctx.render3DPBR(sphere, camera, sphereTransform, materials[r][m], lighting, output, noClear);
            }
        }
    }

    // Animated gold torus
    float torusY = 1.5f + std::sin(t) * 0.2f;
    glm::mat4 torusTransform = glm::translate(glm::mat4(1.0f), glm::vec3(3.5f, torusY, 0.0f));
    torusTransform = glm::rotate(torusTransform, t * 0.5f, glm::vec3(0, 1, 0));
    torusTransform = glm::rotate(torusTransform, t * 0.3f, glm::vec3(1, 0, 0));

    if (hasIBL && useIBL) {
        ctx.render3DPBR(torus, camera, torusTransform, torusMaterial, lighting, iblEnvironment, output, noClear);
    } else {
        ctx.render3DPBR(torus, camera, torusTransform, torusMaterial, lighting, output, noClear);
    }

    // Silver torus on other side
    PBRMaterial silverTorus = PBRMaterial::silver();
    glm::mat4 silverTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-3.5f, torusY, 0.0f));
    silverTransform = glm::rotate(silverTransform, -t * 0.4f, glm::vec3(0, 1, 0));
    silverTransform = glm::rotate(silverTransform, -t * 0.25f, glm::vec3(0, 0, 1));

    if (hasIBL && useIBL) {
        ctx.render3DPBR(torus, camera, silverTransform, silverTorus, lighting, iblEnvironment, output, noClear);
    } else {
        ctx.render3DPBR(torus, camera, silverTransform, silverTorus, lighting, output, noClear);
    }

    // Copper sphere with rough surface
    PBRMaterial roughCopper = PBRMaterial::copper();
    roughCopper.roughness = 0.6f;
    glm::mat4 copperTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.8f, 3.5f));
    copperTransform = glm::scale(copperTransform, glm::vec3(1.5f));

    if (hasIBL && useIBL) {
        ctx.render3DPBR(sphere, camera, copperTransform, roughCopper, lighting, iblEnvironment, output, noClear);
    } else {
        ctx.render3DPBR(sphere, camera, copperTransform, roughCopper, lighting, output, noClear);
    }

    // Emissive cylinders (glowing pedestals)
    float pulseIntensity = 0.5f + 0.5f * std::sin(t * 2.0f);

    // Blue emissive
    PBRMaterial blueGlow = emissiveMaterial;
    blueGlow.emissive = glm::vec3(0.1f, 0.3f, 1.0f) * pulseIntensity * 2.0f;
    glm::mat4 cylinder1Transform = glm::translate(glm::mat4(1.0f), glm::vec3(-4.5f, 0.4f, -3.5f));

    if (hasIBL && useIBL) {
        ctx.render3DPBR(cylinder, camera, cylinder1Transform, blueGlow, lighting, iblEnvironment, output, noClear);
    } else {
        ctx.render3DPBR(cylinder, camera, cylinder1Transform, blueGlow, lighting, output, noClear);
    }

    // Orange emissive
    PBRMaterial orangeGlow = emissiveMaterial;
    orangeGlow.emissive = glm::vec3(1.0f, 0.4f, 0.1f) * (1.0f - pulseIntensity + 0.5f) * 2.0f;
    glm::mat4 cylinder2Transform = glm::translate(glm::mat4(1.0f), glm::vec3(4.5f, 0.4f, -3.5f));

    if (hasIBL && useIBL) {
        ctx.render3DPBR(cylinder, camera, cylinder2Transform, orangeGlow, lighting, iblEnvironment, output, noClear);
    } else {
        ctx.render3DPBR(cylinder, camera, cylinder2Transform, orangeGlow, lighting, output, noClear);
    }

    // Green emissive
    PBRMaterial greenGlow = emissiveMaterial;
    greenGlow.emissive = glm::vec3(0.2f, 1.0f, 0.3f) * pulseIntensity * 1.5f;
    glm::mat4 cylinder3Transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.4f, -4.5f));

    if (hasIBL && useIBL) {
        ctx.render3DPBR(cylinder, camera, cylinder3Transform, greenGlow, lighting, iblEnvironment, output, noClear);
    } else {
        ctx.render3DPBR(cylinder, camera, cylinder3Transform, greenGlow, lighting, output, noClear);
    }

    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
