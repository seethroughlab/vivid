// Decal Demo
// Demonstrates screen-space decal projection onto 3D geometry
// Shows multiple blend modes and animated decals
// Mouse drag to orbit camera, scroll to zoom

#include <vivid/vivid.h>
#include <iostream>
#include <cmath>

using namespace vivid;

// Scene objects
static Mesh3D cube;
static Mesh3D sphere;
static Mesh3D groundPlane;
static Camera3D camera;
static Texture output;

// Lighting
static SceneLighting lighting;

// Materials
static PBRMaterial groundMaterial;
static PBRMaterial cubeMaterial;
static PBRMaterial sphereMaterial;

// Decal textures (procedurally generated)
static Texture decalTex1;
static Texture decalTex2;
static Texture decalTex3;

// Camera control
static float lastMouseX = 0;
static float lastMouseY = 0;
static bool isDragging = false;

void setup(Chain& chain) {
    chain.setOutput("out");
}

// Generate a simple circle texture for decals
void generateCircleTexture(Context& ctx, Texture& tex, glm::vec3 color) {
    const int size = 128;
    std::vector<uint8_t> pixels(size * size * 4);

    float cx = size / 2.0f;
    float cy = size / 2.0f;
    float radius = size / 2.0f - 2.0f;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = x - cx;
            float dy = y - cy;
            float dist = std::sqrt(dx*dx + dy*dy);

            int idx = (y * size + x) * 4;
            if (dist < radius) {
                float alpha = 1.0f - std::pow(dist / radius, 4.0f);
                pixels[idx + 0] = uint8_t(color.r * 255);
                pixels[idx + 1] = uint8_t(color.g * 255);
                pixels[idx + 2] = uint8_t(color.b * 255);
                pixels[idx + 3] = uint8_t(alpha * 255);
            } else {
                pixels[idx + 0] = 0;
                pixels[idx + 1] = 0;
                pixels[idx + 2] = 0;
                pixels[idx + 3] = 0;
            }
        }
    }

    tex = ctx.createTexture(size, size);
    ctx.uploadTexturePixels(tex, pixels.data(), size, size);
}

// Generate a crosshair/target texture
void generateTargetTexture(Context& ctx, Texture& tex, glm::vec3 color) {
    const int size = 128;
    std::vector<uint8_t> pixels(size * size * 4);

    float cx = size / 2.0f;
    float cy = size / 2.0f;
    float outerRadius = size / 2.0f - 4.0f;
    float innerRadius = outerRadius * 0.6f;
    float ringWidth = 4.0f;
    float crossWidth = 3.0f;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = x - cx;
            float dy = y - cy;
            float dist = std::sqrt(dx*dx + dy*dy);

            int idx = (y * size + x) * 4;
            float alpha = 0.0f;

            // Outer ring
            if (dist > outerRadius - ringWidth && dist < outerRadius) {
                alpha = 1.0f;
            }
            // Inner ring
            if (dist > innerRadius - ringWidth && dist < innerRadius) {
                alpha = 1.0f;
            }
            // Cross
            if ((std::abs(dx) < crossWidth || std::abs(dy) < crossWidth) && dist < outerRadius) {
                alpha = 1.0f;
            }

            pixels[idx + 0] = uint8_t(color.r * 255);
            pixels[idx + 1] = uint8_t(color.g * 255);
            pixels[idx + 2] = uint8_t(color.b * 255);
            pixels[idx + 3] = uint8_t(alpha * 255);
        }
    }

    tex = ctx.createTexture(size, size);
    ctx.uploadTexturePixels(tex, pixels.data(), size, size);
}

void update(Chain& chain, Context& ctx) {
    // Create meshes and textures on first frame
    if (!cube.valid()) {
        cube = ctx.createCube();
        sphere = ctx.createSphere(0.6f, 32, 24);
        groundPlane = ctx.createPlane(10.0f, 10.0f);
        output = ctx.createTexture();

        // Generate decal textures
        generateCircleTexture(ctx, decalTex1, glm::vec3(1.0f, 0.2f, 0.2f));  // Red
        generateCircleTexture(ctx, decalTex2, glm::vec3(0.2f, 0.8f, 0.2f));  // Green
        generateTargetTexture(ctx, decalTex3, glm::vec3(0.2f, 0.5f, 1.0f));  // Blue target

        // Setup camera
        camera.position = glm::vec3(5.0f, 4.0f, 6.0f);
        camera.target = glm::vec3(0.0f, 0.5f, 0.0f);
        camera.fov = 45.0f;

        // Setup lighting
        lighting.ambientColor = glm::vec3(0.1f, 0.1f, 0.15f);
        lighting.ambientIntensity = 0.2f;

        // Key light
        lighting.addLight(Light::directional(
            glm::vec3(-0.5f, -1.0f, -0.3f),
            glm::vec3(1.0f, 0.95f, 0.85f),
            0.8f
        ));

        // Fill light
        lighting.addLight(Light::directional(
            glm::vec3(0.8f, -0.3f, 0.5f),
            glm::vec3(0.5f, 0.6f, 0.9f),
            0.3f
        ));

        // Materials
        groundMaterial.albedo = glm::vec3(0.4f, 0.4f, 0.4f);
        groundMaterial.metallic = 0.0f;
        groundMaterial.roughness = 0.9f;

        cubeMaterial.albedo = glm::vec3(0.8f, 0.75f, 0.7f);
        cubeMaterial.metallic = 0.0f;
        cubeMaterial.roughness = 0.5f;

        sphereMaterial.albedo = glm::vec3(0.7f, 0.7f, 0.8f);
        sphereMaterial.metallic = 0.3f;
        sphereMaterial.roughness = 0.4f;

        std::cout << "\n=== Decal Demo ===\n";
        std::cout << "Demonstrates screen-space decal projection\n";
        std::cout << "Drag mouse to orbit camera\n";
        std::cout << "Scroll to zoom\n\n";
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

    // Animation time
    float t = ctx.time();

    // Clear colors
    glm::vec4 clearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glm::vec4 noClear(0.0f, 0.0f, 0.0f, -1.0f);

    // Ground plane transform (rotated to be horizontal)
    glm::mat4 groundTransform = glm::rotate(glm::mat4(1.0f), -glm::half_pi<float>(), glm::vec3(1, 0, 0));

    // Cube transform
    glm::mat4 cubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.5f, 0.0f));

    // Sphere transform
    glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.6f, 0.0f));

    // Render the 3D scene
    ctx.render3D(groundPlane, camera, groundTransform, groundMaterial, lighting, output, clearColor);
    ctx.render3D(cube, camera, cubeTransform, cubeMaterial, lighting, output, noClear);
    ctx.render3D(sphere, camera, sphereTransform, sphereMaterial, lighting, output, noClear);

    // Get the depth texture from the scene render
    Texture depthTex = ctx.getSceneDepthTexture();

    // Create decals
    std::vector<Decal> decals;

    // Decal 1: Red circle on ground (animated position)
    Decal decal1;
    decal1.texture = &decalTex1;
    decal1.position = glm::vec3(std::sin(t * 0.5f) * 2.0f, 1.0f, std::cos(t * 0.5f) * 2.0f);
    decal1.rotation = glm::vec3(-90.0f, 0.0f, 0.0f);  // Project downward
    decal1.size = glm::vec3(1.5f, 1.5f, 2.0f);
    decal1.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.8f);
    decal1.blendMode = DecalBlendMode::Normal;
    decals.push_back(decal1);

    // Decal 2: Green circle on cube (from the side)
    Decal decal2;
    decal2.texture = &decalTex2;
    decal2.position = glm::vec3(-1.5f, 0.5f, 1.5f);  // Position in front of cube
    decal2.rotation = glm::vec3(0.0f, 0.0f, 0.0f);   // Project along -Z
    decal2.size = glm::vec3(0.8f, 0.8f, 2.0f);
    decal2.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    decal2.blendMode = DecalBlendMode::Normal;
    decals.push_back(decal2);

    // Decal 3: Blue target on sphere (animated rotation)
    Decal decal3;
    decal3.texture = &decalTex3;
    decal3.position = glm::vec3(1.5f, 0.6f, 1.5f);
    decal3.rotation = glm::vec3(0.0f, t * 20.0f, 0.0f);  // Rotating
    decal3.size = glm::vec3(0.6f, 0.6f, 2.0f);
    decal3.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.9f);
    decal3.blendMode = DecalBlendMode::Additive;  // Glowing effect
    decals.push_back(decal3);

    // Decal 4: Multiply decal on ground (dirt/shadow)
    Decal decal4;
    decal4.texture = &decalTex1;
    decal4.position = glm::vec3(0.0f, 0.5f, -2.0f);
    decal4.rotation = glm::vec3(-90.0f, 0.0f, 0.0f);
    decal4.size = glm::vec3(2.0f, 2.0f, 1.0f);
    decal4.color = glm::vec4(0.3f, 0.25f, 0.2f, 0.7f);  // Dark brown tint
    decal4.blendMode = DecalBlendMode::Multiply;
    decals.push_back(decal4);

    // Render all decals
    ctx.renderDecals(decals, camera, depthTex, output);

    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
