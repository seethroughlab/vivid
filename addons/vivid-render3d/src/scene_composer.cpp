// SceneComposer preview rendering implementation
// Renders a rotating 3D preview of the composed scene for the chain visualizer

#include <vivid/render3d/scene_composer.h>
#include <vivid/render3d/renderer.h>
#include <vivid/render3d/camera_operator.h>
#include <vivid/context.h>
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cfloat>

namespace vivid::render3d {

// Preview thumbnail size (16:9 aspect ratio)
static constexpr int PREVIEW_WIDTH = 100;
static constexpr int PREVIEW_HEIGHT = 56;

void SceneComposer::updateScenePreview(Context& ctx) {
    // Skip if empty scene
    if (m_scene.empty()) {
        return;
    }

    // Initialize preview renderer lazily
    if (!m_previewRenderer) {
        m_previewCamera = std::make_unique<CameraOperator>();
        m_previewCamera->init(ctx);

        m_previewRenderer = std::make_unique<Render3D>();
        m_previewRenderer->setResolution(PREVIEW_WIDTH, PREVIEW_HEIGHT);
        m_previewRenderer->setShadingMode(ShadingMode::Flat);
        m_previewRenderer->setClearColor(0.12f, 0.14f, 0.18f);
        m_previewRenderer->setAmbient(0.3f);
        m_previewRenderer->setLightDirection(glm::normalize(glm::vec3(1, 2, 1)));
        m_previewRenderer->setCameraInput(m_previewCamera.get());
        m_previewRenderer->init(ctx);
    }

    // Update rotation based on context time
    m_previewRotation = ctx.time() * 0.8f;  // ~0.8 rad/sec

    // Calculate scene bounds for auto-framing
    glm::vec3 minBounds(FLT_MAX), maxBounds(-FLT_MAX);
    int meshCount = 0;
    for (const auto& obj : m_scene.objects()) {
        if (obj.mesh && !obj.mesh->vertices.empty()) {
            for (const auto& v : obj.mesh->vertices) {
                glm::vec3 worldPos = glm::vec3(obj.transform * glm::vec4(v.position, 1.0f));
                minBounds = glm::min(minBounds, worldPos);
                maxBounds = glm::max(maxBounds, worldPos);
            }
            meshCount++;
        }
    }

    if (meshCount == 0) {
        return;
    }

    // Compute center and camera distance
    glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    float maxDist = glm::length(maxBounds - minBounds) * 0.5f;
    float distance = maxDist * 2.5f;
    if (distance < 0.1f) distance = 5.0f;

    // Orbit camera around scene center
    float camX = center.x + distance * 0.7f * cos(m_previewRotation);
    float camZ = center.z + distance * 0.7f * sin(m_previewRotation);
    m_previewCamera->position(camX, center.y + distance * 0.4f, camZ);
    m_previewCamera->target(center.x, center.y, center.z);
    m_previewCamera->fov(45.0f);
    m_previewCamera->nearPlane(0.01f);
    m_previewCamera->farPlane(100.0f);

    // Render (internal use - suppress deprecation warning)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
    m_previewRenderer->setScene(m_scene);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
    m_previewCamera->process(ctx);
    m_previewRenderer->process(ctx);
}

void SceneComposer::cleanupScenePreview() {
    if (m_previewRenderer) {
        m_previewRenderer->cleanup();
        m_previewRenderer.reset();
    }
    if (m_previewCamera) {
        m_previewCamera->cleanup();
        m_previewCamera.reset();
    }
}

bool SceneComposer::drawVisualization(ImDrawList* dl,
                                      float minX, float minY,
                                      float maxX, float maxY) {
    // Display the preview texture if available
    if (m_previewRenderer) {
        WGPUTextureView view = m_previewRenderer->outputView();
        if (view) {
            ImTextureID texId = reinterpret_cast<ImTextureID>(view);
            dl->AddImage(texId, ImVec2(minX, minY), ImVec2(maxX, maxY));
            return true;
        }
    }

    // Fallback: draw a multi-cube icon for scenes
    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;
    float size = std::min(maxX - minX, maxY - minY) * 0.3f;

    // Dark background
    dl->AddRectFilled(ImVec2(minX, minY), ImVec2(maxX, maxY),
                      IM_COL32(30, 50, 70, 255), 4.0f);

    // Draw multiple small cubes to indicate a scene
    ImU32 lineColor = IM_COL32(100, 180, 255, 200);
    float s = size * 0.4f;

    // Left cube
    float lx = cx - size * 0.6f;
    dl->AddRect(ImVec2(lx - s, cy - s), ImVec2(lx + s, cy + s), lineColor, 2.0f, 0, 1.5f);

    // Right cube
    float rx = cx + size * 0.6f;
    dl->AddRect(ImVec2(rx - s, cy - s), ImVec2(rx + s, cy + s), lineColor, 2.0f, 0, 1.5f);

    // Top connecting lines
    dl->AddLine(ImVec2(lx + s, cy - s), ImVec2(rx - s, cy - s), lineColor, 1.0f);
    dl->AddLine(ImVec2(lx + s, cy + s), ImVec2(rx - s, cy + s), lineColor, 1.0f);

    return true;
}

} // namespace vivid::render3d
