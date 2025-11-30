// Model Loader Example with Skeletal Animation
// Demonstrates loading and rendering animated 3D models using the vivid-models addon
//
// Controls:
//   Mouse X: Camera orbit horizontal
//   Mouse Y: Camera orbit vertical
//   Click: Reset camera
//   Space: Switch animation (if multiple)
//   P: Pause/resume animation

#include <vivid/vivid.h>
#include <vivid/models/model_loader.h>
#include <vivid/models/animation_system.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace vivid;

// Store skinned mesh and animation system
static SkinnedMesh3D model;
static models::AnimationSystem animSystem;
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

        // Try to load the Wolf FBX model with animations using the vivid-models addon
        auto parsed = models::parseSkinnedModel(assetDir + "Wolf_One_fbx7.4_binary.fbx");

        // Fallback to other Wolf versions
        if (!parsed.valid()) {
            parsed = models::parseSkinnedModel(assetDir + "Wolf.fbx");
        }

        if (parsed.valid()) {
            // Create GPU mesh from parsed data
            model = ctx.createSkinnedMesh(
                parsed.vertices,
                parsed.indices,
                parsed.skeleton,
                parsed.animations
            );

            // Initialize animation system
            if (animSystem.init(parsed.skeleton, parsed.animations)) {
                // Auto-play the best animation (skip very short ones)
                int bestAnim = 0;
                for (size_t i = 0; i < parsed.animations.size(); ++i) {
                    if (parsed.animations[i].duration > 1.0f) {
                        bestAnim = static_cast<int>(i);
                        break;
                    }
                }
                animSystem.playAnimation(bestAnim, true);
                currentAnimIndex = bestAnim;
            }

            std::cout << "[model-loader] Loaded skinned model with "
                      << model.vertexCount << " vertices, "
                      << model.indexCount / 3 << " triangles\n";
            std::cout << "[model-loader] Skeleton: " << model.skeleton.bones.size() << " bones\n";
            std::cout << "[model-loader] Animations: " << animSystem.animationCount() << "\n";

            for (size_t i = 0; i < animSystem.animationCount(); ++i) {
                std::cout << "  [" << i << "] " << animSystem.animationName(i)
                          << " (" << animSystem.animationDuration(i) << "s)\n";
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

    // Update animation and copy bone matrices to mesh
    if (model.valid() && animSystem.valid()) {
        animSystem.update(ctx.dt());
        model.boneMatrices = animSystem.getBoneMatrices();
    }

    // Handle input
    if (ctx.wasKeyPressed(Key::Space) && animSystem.animationCount() > 0) {
        // Switch to next animation
        currentAnimIndex = (currentAnimIndex + 1) % static_cast<int>(animSystem.animationCount());
        animSystem.playAnimation(currentAnimIndex, true);
        std::cout << "[model-loader] Playing: " << animSystem.animationName(currentAnimIndex) << "\n";
    }

    if (ctx.wasKeyPressed(Key::P)) {
        // Toggle pause
        if (animSystem.isPlaying()) {
            animSystem.pause();
            std::cout << "[model-loader] Paused\n";
        } else {
            animSystem.resume();
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
