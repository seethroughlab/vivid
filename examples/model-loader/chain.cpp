// Model Loader Example
// Demonstrates loading and rendering 3D model files (FBX, OBJ, glTF, etc.)
//
// Controls:
//   Mouse X: Camera orbit horizontal
//   Mouse Y: Camera orbit vertical
//   Click: Reset camera

#include <vivid/vivid.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace vivid;

// Store mesh and camera globally
static Mesh3D model;
static Mesh3D fallbackCube;
static Camera3D camera;
static Texture output;
static bool loadAttempted = false;
static float cameraDistance = 3.0f;

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

        // Paths are relative to project root (where runtime is invoked from)
        const std::string assetDir = "examples/model-loader/assets/";

        // Try to load the Wolf FBX model (FBX 7.4 binary format works with Assimp)
        model = ctx.loadMesh(assetDir + "Wolf_One_fbx7.4_binary.fbx");

        // Fallback to other Wolf versions
        if (!model.valid()) {
            model = ctx.loadMesh(assetDir + "Wolf.fbx");
        }
        // Fallback to generic model files
        if (!model.valid()) {
            model = ctx.loadMesh(assetDir + "model.obj");
        }
        if (!model.valid()) {
            model = ctx.loadMesh(assetDir + "model.gltf");
        }

        if (!model.valid()) {
            // No model found - create a fallback cube
            std::cout << "[model-loader] No model found in assets/\n";
            std::cout << "[model-loader] Supported: FBX, OBJ, glTF, COLLADA, 3DS, etc.\n";
            fallbackCube = ctx.createCube();
            cameraDistance = 3.0f;
        } else {
            std::cout << "[model-loader] Loaded model with "
                      << model.vertexCount << " vertices, "
                      << model.indexCount / 3 << " triangles\n";

            // Auto-fit camera to model bounds
            glm::vec3 center = model.bounds.center();
            glm::vec3 size = model.bounds.size();
            float maxDim = std::max({size.x, size.y, size.z});
            cameraDistance = maxDim * 2.0f;

            camera.target = center;
            camera.position = center + glm::vec3(0, maxDim * 0.5f, cameraDistance);
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
        if (model.valid()) {
            glm::vec3 center = model.bounds.center();
            camera.position = center + glm::vec3(0, cameraDistance * 0.3f, cameraDistance);
        } else {
            camera.position = glm::vec3(0, 1, 3);
        }
    }

    // Create transform (rotate model slowly)
    float t = ctx.time();
    glm::mat4 transform = glm::rotate(glm::mat4(1.0f), t * 0.3f, glm::vec3(0, 1, 0));

    // Render the model (or fallback cube)
    if (model.valid()) {
        ctx.render3D(model, camera, transform, output, glm::vec4(0.1f, 0.1f, 0.15f, 1.0f));
    } else if (fallbackCube.valid()) {
        ctx.render3D(fallbackCube, camera, transform, output, glm::vec4(0.1f, 0.1f, 0.15f, 1.0f));
    }

    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
