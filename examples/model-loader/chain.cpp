// Model Loader Example
// Demonstrates loading and rendering 3D models using the vivid-models addon
//
// Controls:
//   Mouse X: Camera orbit horizontal
//   Mouse Y: Camera orbit vertical
//   Click: Reset camera

#include <vivid/vivid.h>
#include <vivid/models/model_loader.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace vivid;

// Store mesh and camera
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

        // Paths are relative to project folder
        const std::string assetDir = "assets/";

        // Try to load any FBX/glTF model as static mesh
        auto parsed = models::parseModel(assetDir + "Wolf_One_fbx7.4_binary.fbx");

        // Fallback to other models
        if (!parsed.valid()) {
            parsed = models::parseModel(assetDir + "Wolf.fbx");
        }

        if (parsed.valid()) {
            // Create GPU mesh from parsed data
            model = ctx.createMesh(parsed.vertices, parsed.indices);

            std::cout << "[model-loader] Loaded model with "
                      << parsed.vertices.size() << " vertices, "
                      << parsed.indices.size() / 3 << " triangles\n";

            // Camera for model (adjust based on model size)
            cameraDistance = 1.5f;
            camera.target = glm::vec3(0, 0, 0);
            camera.position = glm::vec3(0, 0.5f, cameraDistance);
        } else {
            // No model found - create a fallback cube
            std::cout << "[model-loader] No model found in assets/\n";
            std::cout << "[model-loader] Supported formats: FBX, glTF, OBJ\n";
            fallbackCube = ctx.createCube();
            cameraDistance = 3.0f;
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

    // Rotation transform
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
