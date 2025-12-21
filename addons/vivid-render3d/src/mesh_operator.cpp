// MeshOperator preview rendering implementation
// Renders a rotating 3D preview for the chain visualizer

#include <vivid/render3d/mesh_operator.h>
#include <vivid/render3d/renderer.h>
#include <vivid/render3d/camera_operator.h>
#include <vivid/render3d/scene.h>
#include <vivid/context.h>
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cfloat>

namespace vivid::render3d {

// Preview thumbnail size (16:9 aspect ratio)
static constexpr int PREVIEW_WIDTH = 100;
static constexpr int PREVIEW_HEIGHT = 56;

void MeshOperator::updatePreview(Context& ctx) {
    // Skip if no mesh
    if (!m_mesh.valid()) {
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

        m_previewScene = std::make_unique<Scene>();
    }

    // Update rotation based on context time (consistent animation)
    m_previewRotation = ctx.time() * 0.8f;  // ~0.8 rad/sec

    // Rebuild scene if mesh changed
    if (&m_mesh != m_lastPreviewMesh) {
        m_previewScene->clear();
        // Internal use - suppress deprecation warning
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
        m_previewScene->add(m_mesh, glm::mat4(1.0f), glm::vec4(0.7f, 0.85f, 1.0f, 1.0f));
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
        m_lastPreviewMesh = &m_mesh;
    }

    // Update transform for rotation
    if (!m_previewScene->empty()) {
        m_previewScene->objects()[0].transform =
            glm::rotate(glm::mat4(1.0f), m_previewRotation, glm::vec3(0, 1, 0));
    }

    // Auto-frame camera based on mesh bounds
    if (!m_mesh.vertices.empty()) {
        glm::vec3 center(0);
        float maxDist = 0;
        for (const auto& v : m_mesh.vertices) {
            center += v.position;
        }
        center /= static_cast<float>(m_mesh.vertices.size());
        for (const auto& v : m_mesh.vertices) {
            maxDist = std::max(maxDist, glm::length(v.position - center));
        }
        float distance = maxDist * 2.5f;
        if (distance < 0.1f) distance = 2.0f;  // Fallback for tiny meshes

        m_previewCamera->position(distance * 0.7f, distance * 0.5f, distance * 0.7f);
        m_previewCamera->target(center.x, center.y, center.z);
        m_previewCamera->fov(45.0f);
        m_previewCamera->nearPlane(0.01f);
        m_previewCamera->farPlane(100.0f);
    }

    // Render preview
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
    m_previewRenderer->setScene(*m_previewScene);
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

void MeshOperator::cleanupPreview() {
    if (m_previewRenderer) {
        m_previewRenderer->cleanup();
        m_previewRenderer.reset();
    }
    if (m_previewCamera) {
        m_previewCamera->cleanup();
        m_previewCamera.reset();
    }
    m_previewScene.reset();
    m_lastPreviewMesh = nullptr;
}

bool MeshOperator::drawVisualization(ImDrawList* dl,
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

    // Fallback: draw a simple cube wireframe icon
    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;
    float size = std::min(maxX - minX, maxY - minY) * 0.4f;

    // Dark background
    dl->AddRectFilled(ImVec2(minX, minY), ImVec2(maxX, maxY),
                      IM_COL32(30, 50, 70, 255), 4.0f);

    // Simple isometric cube wireframe
    ImU32 lineColor = IM_COL32(100, 180, 255, 200);
    float s = size * 0.5f;
    float iso = 0.5f;  // Isometric skew

    // Front face
    dl->AddLine(ImVec2(cx - s, cy + s * iso), ImVec2(cx + s, cy + s * iso), lineColor, 1.5f);
    dl->AddLine(ImVec2(cx - s, cy - s + s * iso), ImVec2(cx + s, cy - s + s * iso), lineColor, 1.5f);
    dl->AddLine(ImVec2(cx - s, cy + s * iso), ImVec2(cx - s, cy - s + s * iso), lineColor, 1.5f);
    dl->AddLine(ImVec2(cx + s, cy + s * iso), ImVec2(cx + s, cy - s + s * iso), lineColor, 1.5f);

    // Top lines
    dl->AddLine(ImVec2(cx - s, cy - s + s * iso), ImVec2(cx - s * 0.3f, cy - s - s * 0.3f), lineColor, 1.5f);
    dl->AddLine(ImVec2(cx + s, cy - s + s * iso), ImVec2(cx + s * 0.7f, cy - s - s * 0.3f), lineColor, 1.5f);
    dl->AddLine(ImVec2(cx - s * 0.3f, cy - s - s * 0.3f), ImVec2(cx + s * 0.7f, cy - s - s * 0.3f), lineColor, 1.5f);

    return true;
}

} // namespace vivid::render3d
