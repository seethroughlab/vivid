// Model Loader Example with Skeletal Animation
// Demonstrates loading and rendering animated 3D models (FBX, glTF, etc.)
//
// Controls:
//   Mouse X: Camera orbit horizontal
//   Mouse Y: Camera orbit vertical
//   Click: Reset camera
//   Space: Switch animation (if multiple)
//   P: Pause/resume animation

#include <vivid/vivid.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace vivid;

// Store skinned mesh and camera globally
static SkinnedMesh3D model;
static Mesh3D fallbackCube;
static Camera3D camera;
static Texture output;
static bool loadAttempted = false;
static float cameraDistance = 3.0f;
static int currentAnimIndex = 0;

void setup(Chain& chain) {
    chain.setOutput("out");
}

void update(Chain& chain, Context& ctx) {
    // Create output texture on first frame
    if (!output.valid()) {
        output = ctx.createTexture();

        // Initialize camera
        camera.position = glm::vec3(0, 1, 3);
        camera.target = glm::vec3(0, 0, 0);
        camera.fov = 60.0f;
    }

    // Try to load the model on first frame
    if (!loadAttempted) {
        loadAttempted = true;

        // Paths are relative to project folder
        const std::string assetDir = "assets/";

        // Try to load the Wolf FBX model with animations
        model = ctx.loadSkinnedMesh(assetDir + "Wolf_One_fbx7.4_binary.fbx");

        // Fallback to other Wolf versions
        if (!model.valid()) {
            model = ctx.loadSkinnedMesh(assetDir + "Wolf.fbx");
        }

        if (model.valid()) {
            std::cout << "[model-loader] Loaded skinned model with "
                      << model.vertexCount << " vertices, "
                      << model.indexCount / 3 << " triangles\n";
            std::cout << "[model-loader] Skeleton: " << model.skeleton.bones.size() << " bones\n";
            std::cout << "[model-loader] Animations: " << model.animations.size() << "\n";

            for (size_t i = 0; i < model.animations.size(); ++i) {
                std::cout << "  [" << i << "] " << model.animations[i].name
                          << " (" << model.animations[i].duration << "s)\n";
            }

            // Camera for raw vertex positions (no skinning, small scale ~0.1 units)
            cameraDistance = 1.5f;
            camera.target = glm::vec3(0, 0, 0);
            camera.position = glm::vec3(0, 0.5f, cameraDistance);
        } else {
            // No skinned model found - create a fallback cube
            std::cout << "[model-loader] No animated model found in assets/\n";
            std::cout << "[model-loader] Supported: FBX, glTF with animations\n";
            fallbackCube = ctx.createCube();
            cameraDistance = 3.0f;
        }
    }

    // Update animation
    if (model.valid()) {
        model.update(ctx.dt());
    }

    // Handle input
    if (ctx.wasKeyPressed(Key::Space) && model.hasAnimations()) {
        // Switch to next animation
        currentAnimIndex = (currentAnimIndex + 1) % static_cast<int>(model.animations.size());
        model.playAnimation(currentAnimIndex, true);
        std::cout << "[model-loader] Playing: " << model.animations[currentAnimIndex].name << "\n";
    }

    if (ctx.wasKeyPressed(Key::P)) {
        // Toggle pause
        if (model.player.isPlaying()) {
            model.player.pause();
            std::cout << "[model-loader] Paused\n";
        } else {
            model.player.play();
            std::cout << "[model-loader] Playing\n";
        }
    }

    // Update camera based on mouse
    float orbitX = (ctx.mouseNormX() - 0.5f) * 6.28f;  // Full rotation
    float orbitY = (ctx.mouseNormY() - 0.5f) * 2.0f;   // Partial vertical

    // Calculate camera position on orbit around target
    glm::vec3 target = camera.target;
    camera.position.x = target.x + std::sin(orbitX) * std::cos(orbitY) * cameraDistance;
    camera.position.y = target.y + std::sin(orbitY) * cameraDistance + cameraDistance * 0.2f;
    camera.position.z = target.z + std::cos(orbitX) * std::cos(orbitY) * cameraDistance;

    // Reset on click
    if (ctx.wasMousePressed(0)) {
        camera.position = glm::vec3(0, 2, cameraDistance);
    }

    // Identity transform for now (debugging skinning)
    glm::mat4 transform = glm::mat4(1.0f);

    // Render the model (or fallback cube)
    if (model.valid()) {
        ctx.renderSkinned3D(model, camera, transform, output, glm::vec4(0.1f, 0.1f, 0.15f, 1.0f));
    } else if (fallbackCube.valid()) {
        float t = ctx.time();
        transform = glm::rotate(glm::mat4(1.0f), t * 0.3f, glm::vec3(0, 1, 0));
        ctx.render3D(fallbackCube, camera, transform, output, glm::vec4(0.1f, 0.1f, 0.15f, 1.0f));
    }

    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
