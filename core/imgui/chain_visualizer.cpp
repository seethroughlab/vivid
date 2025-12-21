// Vivid Chain Visualizer Implementation
// Shows registered operators as nodes with connections

#include "chain_visualizer.h"
#include <vivid/audio_operator.h>
#include <vivid/audio_graph.h>
#include <vivid/audio/levels.h>
#include <vivid/audio/fft.h>
#include <vivid/audio/band_split.h>
#include <vivid/audio/beat_detect.h>
// Drum synths for visualization
#include <vivid/audio/kick.h>
#include <vivid/audio/snare.h>
#include <vivid/audio/hihat.h>
#include <vivid/audio/clap.h>
// Melodic synths for visualization
#include <vivid/audio/synth.h>
#include <vivid/audio/poly_synth.h>
#include <imgui.h>
#include <imnodes.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <filesystem>

// Platform-specific memory monitoring
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace vivid::imgui {

// Special node IDs for output nodes
static constexpr int SCREEN_NODE_ID = 9999;
static constexpr int SPEAKERS_NODE_ID = 9998;

// Thumbnail sizes (16:9 aspect ratio)
static constexpr float THUMB_WIDTH = 100.0f;
static constexpr float THUMB_HEIGHT = 56.0f;
static constexpr float FOCUSED_SCALE = 3.0f;  // 3x larger when focused

// Get process memory usage in bytes
static size_t getProcessMemoryUsage() {
#if defined(__APPLE__)
    task_vm_info_data_t vmInfo;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vmInfo, &count) == KERN_SUCCESS) {
        return vmInfo.phys_footprint;  // Actual physical memory used
    }
    return 0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        size_t size, resident;
        statm >> size >> resident;
        return resident * sysconf(_SC_PAGESIZE);
    }
    return 0;
#else
    return 0;
#endif
}

// Format bytes as human-readable string (MB or GB)
static std::string formatMemory(size_t bytes) {
    char buf[32];
    if (bytes >= 1024 * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    } else {
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    }
    return buf;
}

using namespace vivid::render3d;

ChainVisualizer::~ChainVisualizer() {
    shutdown();
}

void ChainVisualizer::init() {
    if (m_initialized) return;

    ImNodes::CreateContext();
    ImNodes::StyleColorsDark();

    // Enable trackpad-friendly panning: Control + Left Click to pan
    // (Can't use Cmd on Mac - system intercepts Cmd+Click)
    ImNodesIO& io = ImNodes::GetIO();
    io.EmulateThreeButtonMouse.Modifier = &ImGui::GetIO().KeyCtrl;

    // Configure style
    ImNodesStyle& style = ImNodes::GetStyle();
    style.NodeCornerRounding = 4.0f;
    style.NodePadding = ImVec2(8.0f, 8.0f);
    style.LinkThickness = 3.0f;
    style.PinCircleRadius = 4.0f;

    // Transparent background - nodes float over the chain output
    ImNodes::GetStyle().Colors[ImNodesCol_GridBackground] = IM_COL32(0, 0, 0, 0);  // Fully transparent
    ImNodes::GetStyle().Colors[ImNodesCol_GridLine] = IM_COL32(60, 60, 80, 40);     // Very subtle grid
    ImNodes::GetStyle().Colors[ImNodesCol_GridLinePrimary] = IM_COL32(80, 80, 100, 60);

    // Semi-transparent node backgrounds
    ImNodes::GetStyle().Colors[ImNodesCol_NodeBackground] = IM_COL32(30, 30, 40, 200);
    ImNodes::GetStyle().Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(40, 40, 55, 220);
    ImNodes::GetStyle().Colors[ImNodesCol_NodeBackgroundSelected] = IM_COL32(50, 50, 70, 240);

    m_initialized = true;
}

void ChainVisualizer::shutdown() {
    if (!m_initialized) return;

    // Exit solo mode if active
    if (m_inSoloMode) {
        exitSoloMode();
    }

    // Clean up solo geometry renderer
    if (m_soloGeometryRenderer) {
        m_soloGeometryRenderer->cleanup();
        m_soloGeometryRenderer.reset();
    }

    // Clean up geometry preview renderers
    for (auto& [op, preview] : m_geometryPreviews) {
        if (preview.renderer) {
            preview.renderer->cleanup();
        }
    }
    m_geometryPreviews.clear();

    ImNodes::DestroyContext();
    m_initialized = false;
    m_layoutBuilt = false;
    m_opToNodeId.clear();
    m_nodePositioned.clear();
}

void ChainVisualizer::selectNodeFromEditor(const std::string& operatorName) {
    // Store the selection to be applied in next render() call
    // (ImNodes calls must happen within the node editor context)
    m_pendingEditorSelection = operatorName;
}

// Estimate node height based on content (parameters are now in inspector panel)
float ChainVisualizer::estimateNodeHeight(const vivid::OperatorInfo& info) const {
    float height = 0.0f;

    // Title bar
    height += 24.0f;

    // Type name (if different from registered name)
    if (info.op && info.op->name() != info.name) {
        height += 18.0f;
    }

    // Input pins (~20px each)
    if (info.op) {
        int inputCount = 0;
        for (size_t j = 0; j < info.op->inputCount(); ++j) {
            if (info.op->getInput(static_cast<int>(j))) {
                inputCount = static_cast<int>(j) + 1;
            }
        }
        height += inputCount * 20.0f;
    }

    // Thumbnail/preview area
    if (info.op) {
        vivid::OutputKind kind = info.op->outputKind();
        if (kind == vivid::OutputKind::Texture || kind == vivid::OutputKind::Geometry) {
            height += 60.0f;  // 56px image + padding
        } else {
            height += 54.0f;  // Icons are slightly smaller
        }
    }

    // Output pin
    height += 20.0f;

    // Node padding
    height += 16.0f;

    return height;
}

void ChainVisualizer::buildLayout(const std::vector<vivid::OperatorInfo>& operators) {
    m_opToNodeId.clear();
    m_nodePositioned.clear();
    m_nodePositioned[SCREEN_NODE_ID] = false;    // Reset Screen node position on layout rebuild
    m_nodePositioned[SPEAKERS_NODE_ID] = false;  // Reset Speakers node position on layout rebuild

    // Assign node IDs to operators
    for (size_t i = 0; i < operators.size(); ++i) {
        if (operators[i].op) {
            m_opToNodeId[operators[i].op] = static_cast<int>(i);
        }
    }

    // Calculate depth for each operator (distance from sources)
    std::vector<int> depths(operators.size(), 0);

    for (size_t i = 0; i < operators.size(); ++i) {
        vivid::Operator* op = operators[i].op;
        if (!op) continue;

        int maxInputDepth = -1;
        for (size_t j = 0; j < op->inputCount(); ++j) {
            vivid::Operator* input = op->getInput(static_cast<int>(j));
            if (input && m_opToNodeId.count(input)) {
                int inputNodeId = m_opToNodeId[input];
                maxInputDepth = std::max(maxInputDepth, depths[inputNodeId]);
            }
        }
        depths[i] = maxInputDepth + 1;
    }

    // Group operators by depth
    int maxDepth = 0;
    for (int d : depths) maxDepth = std::max(maxDepth, d);

    std::vector<std::vector<int>> columns(maxDepth + 1);
    for (size_t i = 0; i < operators.size(); ++i) {
        columns[depths[i]].push_back(static_cast<int>(i));
    }

    // Position nodes in columns using estimated heights
    const float xSpacing = 280.0f;   // Horizontal space between columns
    const float verticalPadding = 20.0f;  // Extra space between nodes
    const float startX = 50.0f;
    const float startY = 50.0f;

    for (int col = 0; col < static_cast<int>(columns.size()); ++col) {
        float y = startY;

        for (size_t idx = 0; idx < columns[col].size(); ++idx) {
            int nodeId = columns[col][idx];
            float x = startX + col * xSpacing;
            ImNodes::SetNodeGridSpacePos(nodeId, ImVec2(x, y));
            m_nodePositioned[nodeId] = true;

            // Move y down by this node's estimated height + padding
            float nodeHeight = estimateNodeHeight(operators[nodeId]);
            y += nodeHeight + verticalPadding;
        }
    }

    m_layoutBuilt = true;
}

void ChainVisualizer::updateGeometryPreview(
    GeometryPreview& preview,
    render3d::Mesh* mesh,
    vivid::Context& ctx,
    float dt
) {
    // Initialize renderer and camera if needed
    if (!preview.renderer) {
        preview.cameraOp = std::make_unique<render3d::CameraOperator>();
        preview.cameraOp->init(ctx);

        preview.renderer = std::make_unique<render3d::Render3D>();
        preview.renderer->setResolution(100, 56);
        preview.renderer->setShadingMode(render3d::ShadingMode::Flat);
        preview.renderer->setClearColor(0.12f, 0.14f, 0.18f);
        preview.renderer->setAmbient(0.3f);
        preview.renderer->setLightDirection(glm::normalize(glm::vec3(1, 2, 1)));
        preview.renderer->setCameraInput(preview.cameraOp.get());
        preview.renderer->init(ctx);
    }

    // Update rotation
    preview.rotationAngle += dt * 0.8f;  // ~0.8 rad/sec rotation

    // Rebuild scene if mesh changed
    if (mesh != preview.lastMesh) {
        preview.scene.clear();
        if (mesh) {
            // Internal use - suppress deprecation warning (this is internal preview code)
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
            preview.scene.add(*mesh, glm::mat4(1.0f), glm::vec4(0.7f, 0.85f, 1.0f, 1.0f));
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
        }
        preview.lastMesh = mesh;
    }

    // Update transform for rotation
    if (mesh && !preview.scene.empty()) {
        preview.scene.objects()[0].transform =
            glm::rotate(glm::mat4(1.0f), preview.rotationAngle, glm::vec3(0, 1, 0));
    }

    // Auto-frame camera based on mesh bounds (only compute once per mesh)
    if (mesh && !mesh->vertices.empty()) {
        glm::vec3 center(0);
        float maxDist = 0;
        for (const auto& v : mesh->vertices) {
            center += v.position;
        }
        center /= static_cast<float>(mesh->vertices.size());
        for (const auto& v : mesh->vertices) {
            maxDist = std::max(maxDist, glm::length(v.position - center));
        }
        float distance = maxDist * 2.5f;
        if (distance < 0.1f) distance = 2.0f;  // Fallback for tiny meshes
        preview.cameraOp->position(distance * 0.7f, distance * 0.5f, distance * 0.7f);
        preview.cameraOp->target(center.x, center.y, center.z);
        preview.cameraOp->fov(45.0f);
        preview.cameraOp->nearPlane(0.01f);
        preview.cameraOp->farPlane(100.0f);
    }

    // Render (internal use - suppress deprecation warning for scene())
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
    preview.renderer->setScene(preview.scene);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
    preview.cameraOp->process(ctx);
    preview.renderer->process(ctx);
}

void ChainVisualizer::updateScenePreview(
    GeometryPreview& preview,
    render3d::SceneComposer* composer,
    vivid::Context& ctx,
    float dt
) {
    // Initialize renderer and camera if needed
    if (!preview.renderer) {
        preview.cameraOp = std::make_unique<render3d::CameraOperator>();
        preview.cameraOp->init(ctx);

        preview.renderer = std::make_unique<render3d::Render3D>();
        preview.renderer->setResolution(100, 56);
        preview.renderer->setShadingMode(render3d::ShadingMode::Flat);
        preview.renderer->setClearColor(0.12f, 0.14f, 0.18f);
        preview.renderer->setAmbient(0.3f);
        preview.renderer->setLightDirection(glm::normalize(glm::vec3(1, 2, 1)));
        preview.renderer->setCameraInput(preview.cameraOp.get());
        preview.renderer->init(ctx);
    }

    // Update rotation
    preview.rotationAngle += dt * 0.8f;

    // Get the composed scene
    render3d::Scene& scene = composer->outputScene();

    if (scene.empty()) {
        return;
    }

    // Calculate scene bounds for auto-framing (across all objects)
    glm::vec3 minBounds(FLT_MAX), maxBounds(-FLT_MAX);
    int meshCount = 0;
    for (const auto& obj : scene.objects()) {
        if (obj.mesh && !obj.mesh->vertices.empty()) {
            for (const auto& v : obj.mesh->vertices) {
                // Transform vertex to world space
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
    float camX = center.x + distance * 0.7f * cos(preview.rotationAngle);
    float camZ = center.z + distance * 0.7f * sin(preview.rotationAngle);
    preview.cameraOp->position(camX, center.y + distance * 0.4f, camZ);
    preview.cameraOp->target(center.x, center.y, center.z);
    preview.cameraOp->fov(45.0f);
    preview.cameraOp->nearPlane(0.01f);
    preview.cameraOp->farPlane(100.0f);

    // Render (internal use - suppress deprecation warning for scene())
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
    preview.renderer->setScene(scene);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
    preview.cameraOp->process(ctx);
    preview.renderer->process(ctx);
}

void ChainVisualizer::enterSoloMode(vivid::Operator* op, const std::string& name) {
    m_soloOperator = op;
    m_soloOperatorName = name;
    m_inSoloMode = true;
    m_soloRotationAngle = 0.0f;
}

void ChainVisualizer::exitSoloMode() {
    m_soloOperator = nullptr;
    m_soloOperatorName.clear();
    m_inSoloMode = false;

    // Clean up solo geometry renderer
    if (m_soloGeometryRenderer) {
        m_soloGeometryRenderer->cleanup();
        m_soloGeometryRenderer.reset();
    }
}

void ChainVisualizer::renderSoloOverlay(const FrameInput& input, vivid::Context& ctx) {
    if (!m_soloOperator) {
        exitSoloMode();
        return;
    }

    vivid::OutputKind kind = m_soloOperator->outputKind();

    if (kind == vivid::OutputKind::Texture) {
        // For texture operators, just set the output texture
        WGPUTextureView view = m_soloOperator->outputView();
        if (view) {
            ctx.setOutputTexture(view);
        }
    } else if (kind == vivid::OutputKind::Geometry) {
        // For geometry operators, render at full viewport size
        m_soloRotationAngle += input.dt * 0.8f;

        // Initialize or resize renderer and camera
        if (!m_soloGeometryRenderer) {
            m_soloCameraOp = std::make_unique<render3d::CameraOperator>();
            m_soloCameraOp->init(ctx);

            m_soloGeometryRenderer = std::make_unique<render3d::Render3D>();
            m_soloGeometryRenderer->setShadingMode(render3d::ShadingMode::Flat);
            m_soloGeometryRenderer->setClearColor(0.08f, 0.1f, 0.14f);
            m_soloGeometryRenderer->setAmbient(0.3f);
            m_soloGeometryRenderer->setLightDirection(glm::normalize(glm::vec3(1, 2, 1)));
            m_soloGeometryRenderer->setCameraInput(m_soloCameraOp.get());
            m_soloGeometryRenderer->init(ctx);
        }

        // Resize to match viewport
        m_soloGeometryRenderer->setResolution(input.width, input.height);

        // Check if this is a SceneComposer
        auto* composer = dynamic_cast<render3d::SceneComposer*>(m_soloOperator);

        if (composer) {
            // Render composed scene
            render3d::Scene& scene = composer->outputScene();
            if (!scene.empty()) {
                // Calculate scene bounds
                glm::vec3 minBounds(FLT_MAX), maxBounds(-FLT_MAX);
                for (const auto& obj : scene.objects()) {
                    if (obj.mesh && !obj.mesh->vertices.empty()) {
                        for (const auto& v : obj.mesh->vertices) {
                            glm::vec3 worldPos = glm::vec3(obj.transform * glm::vec4(v.position, 1.0f));
                            minBounds = glm::min(minBounds, worldPos);
                            maxBounds = glm::max(maxBounds, worldPos);
                        }
                    }
                }

                glm::vec3 center = (minBounds + maxBounds) * 0.5f;
                float maxDist = glm::length(maxBounds - minBounds) * 0.5f;
                float distance = maxDist * 2.5f;
                if (distance < 0.1f) distance = 5.0f;

                // Orbit camera
                float camX = center.x + distance * 0.7f * cos(m_soloRotationAngle);
                float camZ = center.z + distance * 0.7f * sin(m_soloRotationAngle);
                m_soloCameraOp->position(camX, center.y + distance * 0.4f, camZ);
                m_soloCameraOp->target(center.x, center.y, center.z);
                m_soloCameraOp->fov(45.0f);
                m_soloCameraOp->nearPlane(0.01f);
                m_soloCameraOp->farPlane(1000.0f);

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
                m_soloGeometryRenderer->setScene(scene);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
                m_soloCameraOp->process(ctx);
                m_soloGeometryRenderer->process(ctx);
                ctx.setOutputTexture(m_soloGeometryRenderer->outputView());
            }
        } else if (auto* meshOp = dynamic_cast<render3d::MeshOperator*>(m_soloOperator)) {
            // Render single mesh
            render3d::Mesh* mesh = meshOp->outputMesh();
            if (mesh && mesh->valid()) {
                // Build scene with rotating mesh
                render3d::Scene scene;
                glm::mat4 transform = glm::rotate(glm::mat4(1.0f), m_soloRotationAngle, glm::vec3(0, 1, 0));
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
                scene.add(*mesh, transform, glm::vec4(0.7f, 0.85f, 1.0f, 1.0f));
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

                // Auto-frame camera
                glm::vec3 center(0);
                float maxDist = 0;
                for (const auto& v : mesh->vertices) {
                    center += v.position;
                }
                center /= static_cast<float>(mesh->vertices.size());
                for (const auto& v : mesh->vertices) {
                    maxDist = std::max(maxDist, glm::length(v.position - center));
                }
                float distance = maxDist * 2.5f;
                if (distance < 0.1f) distance = 2.0f;

                m_soloCameraOp->position(distance * 0.7f, distance * 0.5f, distance * 0.7f);
                m_soloCameraOp->target(center.x, center.y, center.z);
                m_soloCameraOp->fov(45.0f);
                m_soloCameraOp->nearPlane(0.01f);
                m_soloCameraOp->farPlane(100.0f);

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
                m_soloGeometryRenderer->setScene(scene);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
                m_soloCameraOp->process(ctx);
                m_soloGeometryRenderer->process(ctx);
                ctx.setOutputTexture(m_soloGeometryRenderer->outputView());
            }
        }
    }

    // Check for Escape key to exit solo mode
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        exitSoloMode();
        return;
    }

    // Draw solo mode overlay (semi-transparent)
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowBgAlpha(0.5f);  // Semi-transparent background
    ImGui::Begin("Solo Mode", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "SOLO: %s", m_soloOperatorName.c_str());
    if (ImGui::Button("Exit Solo")) {
        exitSoloMode();
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "(or press ESC)");
    ImGui::End();
}

void ChainVisualizer::render(const FrameInput& input, vivid::Context& ctx) {
    if (!m_initialized) {
        init();
    }

    // Handle Escape key to exit solo mode
    if (m_inSoloMode && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        exitSoloMode();
    }

    // If in solo mode, render the solo view instead of normal UI
    if (m_inSoloMode) {
        renderSoloOverlay(input, ctx);
        return;  // Don't show the node editor in solo mode
    }

    const auto& operators = ctx.registeredOperators();

    // Menu bar with performance stats and controls
    float fps = input.dt > 0 ? 1.0f / input.dt : 0.0f;
    if (ImGui::BeginMainMenuBar()) {
        // Performance stats on the left
        ImGui::Text("%.1f FPS", fps);
        ImGui::Separator();
        ImGui::Text("%.2fms", input.dt * 1000.0f);
        ImGui::Separator();
        ImGui::Text("%dx%d", input.width, input.height);
        ImGui::Separator();
        ImGui::Text("%zu ops", operators.size());

        // Memory usage display
        ImGui::Separator();
        size_t memBytes = getProcessMemoryUsage();
        std::string memStr = formatMemory(memBytes);
        // Color based on memory usage: green < 500MB, yellow < 2GB, red >= 2GB
        ImVec4 memColor;
        if (memBytes < 500 * 1024 * 1024) {
            memColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);  // Green
        } else if (memBytes < 2ULL * 1024 * 1024 * 1024) {
            memColor = ImVec4(0.9f, 0.9f, 0.4f, 1.0f);  // Yellow
        } else {
            memColor = ImVec4(0.9f, 0.4f, 0.4f, 1.0f);  // Red
        }
        ImGui::TextColored(memColor, "MEM: %s", memStr.c_str());

        // Audio stats display (if audio is active)
        AudioGraph* audioGraph = ctx.chain().audioGraph();
        if (audioGraph && !audioGraph->empty()) {
            ImGui::Separator();

            // DSP Load with color coding
            float dspLoad = audioGraph->dspLoad();
            float peakLoad = audioGraph->peakDspLoad();
            ImVec4 dspColor;
            if (dspLoad < 0.5f) {
                dspColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);  // Green
            } else if (dspLoad < 0.8f) {
                dspColor = ImVec4(0.9f, 0.9f, 0.4f, 1.0f);  // Yellow
            } else {
                dspColor = ImVec4(0.9f, 0.4f, 0.4f, 1.0f);  // Red
            }
            ImGui::TextColored(dspColor, "DSP: %.0f%%", dspLoad * 100.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("DSP Load: %.1f%% (Peak: %.1f%%)\nClick to reset peak",
                                  dspLoad * 100.0f, peakLoad * 100.0f);
            }
            if (ImGui::IsItemClicked()) {
                audioGraph->resetPeakDspLoad();
            }

            // Show dropped events if any
            uint64_t dropped = audioGraph->droppedEventCount();
            if (dropped > 0) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "⚠ %llu dropped", dropped);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Audio events dropped (queue overflow)\nClick to reset counter");
                }
                if (ImGui::IsItemClicked()) {
                    audioGraph->resetDroppedEventCount();
                }
            }
        }

        // Controls menu
        if (ImGui::BeginMenu("Controls")) {
            ImGui::MenuItem("Tab: Toggle UI", nullptr, false, false);
            ImGui::MenuItem("F: Fullscreen", nullptr, false, false);
            ImGui::MenuItem("Ctrl+Drag: Pan graph", nullptr, false, false);
            ImGui::MenuItem("S: Solo node", nullptr, false, false);
            ImGui::MenuItem("B: Bypass node", nullptr, false, false);
            ImGui::EndMenu();
        }

        ImGui::Separator();

        // Recording controls
        if (m_exporter.isRecording()) {
            // Red recording indicator
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            ImGui::Text("● REC");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::Text("%d frames (%.1fs)", m_exporter.frameCount(), m_exporter.duration());
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop")) {
                stopRecording(ctx);
            }
        } else {
            if (ImGui::BeginMenu("Record")) {
                if (ImGui::MenuItem("H.264 (recommended)")) {
                    startRecording(ExportCodec::H264, ctx);
                }
                if (ImGui::MenuItem("H.265 (HEVC)")) {
                    startRecording(ExportCodec::H265, ctx);
                }
                if (ImGui::MenuItem("Animation (ProRes 4444)")) {
                    startRecording(ExportCodec::Animation, ctx);
                }
                ImGui::EndMenu();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Snapshot")) {
                requestSnapshot();
            }
        }

        ImGui::EndMainMenuBar();
    }

    // Node editor - transparent, fullscreen overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(input.width), static_cast<float>(input.height)));
    ImGui::SetNextWindowBgAlpha(0.0f);  // Fully transparent background
    ImGui::Begin("Chain Visualizer", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (operators.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
            "No operators registered.");
        ImGui::TextWrapped(
            "Operators are auto-registered when using chain->init(ctx). "
            "Press Tab to hide this UI.");
        ImGui::End();
        return;
    }

    // Build layout if operators changed
    if (!m_layoutBuilt || m_opToNodeId.size() != operators.size()) {
        buildLayout(operators);
    }

    ImNodes::BeginNodeEditor();

    // Render nodes
    for (size_t i = 0; i < operators.size(); ++i) {
        const vivid::OperatorInfo& info = operators[i];
        if (!info.op) continue;

        int nodeId = static_cast<int>(i);

        // Color nodes based on output type
        vivid::OutputKind outputKind = info.op->outputKind();
        bool pushedStyle = false;

        if (outputKind == vivid::OutputKind::Geometry) {
            // Blue-ish tint for geometry nodes
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(40, 80, 120, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(50, 100, 150, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(60, 120, 180, 255));
            pushedStyle = true;
        } else if (outputKind == vivid::OutputKind::Value || outputKind == vivid::OutputKind::ValueArray) {
            // Orange-ish tint for value nodes
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(120, 80, 40, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(150, 100, 50, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(180, 120, 60, 255));
            pushedStyle = true;
        } else if (outputKind == vivid::OutputKind::Camera) {
            // Green-ish tint for camera nodes
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(40, 100, 80, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(50, 125, 100, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(60, 150, 120, 255));
            pushedStyle = true;
        } else if (outputKind == vivid::OutputKind::Light) {
            // Yellow-ish tint for light nodes
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(120, 100, 40, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(150, 125, 50, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(180, 150, 60, 255));
            pushedStyle = true;
        } else if (outputKind == vivid::OutputKind::Audio) {
            // Purple-ish tint for audio nodes
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(100, 60, 120, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(125, 75, 150, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(180, 150, 60, 255));
            pushedStyle = true;
        } else if (outputKind == vivid::OutputKind::AudioValue) {
            // Teal/cyan for audio analysis nodes
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(60, 100, 120, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(75, 125, 150, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(180, 150, 60, 255));
            pushedStyle = true;
        }

        ImNodes::BeginNode(nodeId);

        // Check if bypassed for visual styling
        bool isBypassed = info.op->isBypassed();

        // Title bar - show registered name with Solo and Bypass buttons
        ImNodes::BeginNodeTitleBar();

        // Dim the name if bypassed
        if (isBypassed) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", info.name.c_str());
        } else {
            ImGui::TextUnformatted(info.name.c_str());
        }

        ImGui::SameLine();
        ImGui::PushID(nodeId);

        // Solo button
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(80, 80, 100, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(100, 100, 140, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(120, 120, 180, 255));
        if (ImGui::SmallButton("S")) {
            enterSoloMode(info.op, info.name);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Solo - view full output (or double-click node)");
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        // Bypass button - highlight when active
        if (isBypassed) {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 100, 40, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(200, 120, 60, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(220, 140, 80, 255));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(80, 80, 100, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(100, 100, 140, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(120, 120, 180, 255));
        }
        if (ImGui::SmallButton("B")) {
            info.op->setBypassed(!isBypassed);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(isBypassed ? "Bypass ON - click to enable" : "Bypass - skip this operator");
        }
        ImGui::PopStyleColor(3);

        ImGui::PopID();
        ImNodes::EndNodeTitleBar();

        // Show operator type if different from registered name
        std::string typeName = info.op->name();
        if (typeName != info.name) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "%s", typeName.c_str());
        }

        // Parameters are now shown in the Inspector panel (select node to see)

        // Input pins - show one for each connected input
        int inputCount = 0;
        for (size_t j = 0; j < info.op->inputCount(); ++j) {
            if (info.op->getInput(static_cast<int>(j))) {
                inputCount = static_cast<int>(j) + 1;
            }
        }

        for (int j = 0; j < inputCount; ++j) {
            ImNodes::BeginInputAttribute(inputAttrId(nodeId, j));
            if (inputCount > 1) {
                ImGui::Text("in %d", j);
            } else {
                ImGui::Text("in");
            }
            ImNodes::EndInputAttribute();
        }

        // Thumbnail - render based on output type
        vivid::OutputKind kind = info.op->outputKind();

        // Calculate thumbnail size (3x larger when focused from editor)
        bool nodeFocused = isFocused(info.name);
        float thumbScale = nodeFocused ? FOCUSED_SCALE : 1.0f;
        float thumbW = THUMB_WIDTH * thumbScale;
        float thumbH = THUMB_HEIGHT * thumbScale;

        if (kind == vivid::OutputKind::Texture) {
            WGPUTextureView view = info.op->outputView();
            if (view) {
                // ImGui WebGPU backend accepts WGPUTextureView as ImTextureID
                ImTextureID texId = reinterpret_cast<ImTextureID>(view);
                ImGui::Image(texId, ImVec2(thumbW, thumbH));  // 16:9 aspect ratio (3x when focused)
            } else {
                // Texture operator but no view yet
                ImGui::Dummy(ImVec2(thumbW, thumbH * 0.7f));
                ImVec2 min = ImGui::GetItemRectMin();
                ImVec2 max = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(40, 40, 50, 255), 4.0f);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(min.x + 20, min.y + 12), IM_COL32(100, 100, 120, 255), "no tex");
            }
        } else if (kind == vivid::OutputKind::Geometry) {
            // Geometry operator - render live 3D rotating preview
            auto& preview = m_geometryPreviews[info.op];

            // Check if this is a SceneComposer (outputs Scene, not single Mesh)
            auto* composer = dynamic_cast<render3d::SceneComposer*>(info.op);

            if (composer) {
                // SceneComposer: render the full composed scene
                if (!composer->outputScene().empty()) {
                    updateScenePreview(preview, composer, ctx, input.dt);

                    // Display rendered texture
                    WGPUTextureView view = preview.renderer ? preview.renderer->outputView() : nullptr;
                    if (view) {
                        ImTextureID texId = reinterpret_cast<ImTextureID>(view);
                        ImGui::Image(texId, ImVec2(thumbW, thumbH));
                    } else {
                        ImGui::Dummy(ImVec2(thumbW, thumbH));
                    }
                } else {
                    // Empty scene placeholder
                    ImGui::Dummy(ImVec2(thumbW, thumbH));
                    ImVec2 min = ImGui::GetItemRectMin();
                    ImVec2 max = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(30, 50, 70, 255), 4.0f);
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(min.x + 15, min.y + 20), IM_COL32(100, 180, 255, 255), "empty scene");
                }
            } else {
                // Regular MeshOperator - get mesh and render single object
                render3d::Mesh* mesh = nullptr;
                if (auto* meshOp = dynamic_cast<render3d::MeshOperator*>(info.op)) {
                    mesh = meshOp->outputMesh();
                }

                if (mesh && mesh->valid()) {
                    updateGeometryPreview(preview, mesh, ctx, input.dt);

                    // Display rendered texture
                    WGPUTextureView view = preview.renderer ? preview.renderer->outputView() : nullptr;
                    if (view) {
                        ImTextureID texId = reinterpret_cast<ImTextureID>(view);
                        ImGui::Image(texId, ImVec2(thumbW, thumbH));
                    } else {
                        ImGui::Dummy(ImVec2(thumbW, thumbH));
                    }
                } else {
                    // No valid mesh - show placeholder
                    ImGui::Dummy(ImVec2(thumbW, thumbH));
                    ImVec2 min = ImGui::GetItemRectMin();
                    ImVec2 max = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(30, 50, 70, 255), 4.0f);
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(min.x + 20, min.y + 20), IM_COL32(100, 180, 255, 255), "no mesh");
                }
            }
        } else if (kind == vivid::OutputKind::Value || kind == vivid::OutputKind::ValueArray) {
            // Value operator - show numeric display
            ImGui::Dummy(ImVec2(thumbW, thumbH * 0.7f));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(50, 40, 30, 255), 4.0f);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(min.x + 25, min.y + 12), IM_COL32(200, 180, 100, 255),
                kind == vivid::OutputKind::Value ? "Value" : "Values");
        } else if (kind == vivid::OutputKind::Camera) {
            // Camera operator - draw camera icon
            ImGui::Dummy(ImVec2(thumbW, thumbH * 0.9f));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(30, 60, 50, 255), 4.0f);

            // Draw camera body (rectangle) - scale icon with thumbnail
            float cx = (min.x + max.x) * 0.5f;
            float cy = (min.y + max.y) * 0.5f;
            float iconScale = thumbScale;
            ImU32 iconColor = IM_COL32(100, 200, 160, 255);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(cx - 20*iconScale, cy - 10*iconScale), ImVec2(cx + 10*iconScale, cy + 10*iconScale), iconColor, 3.0f);
            // Draw lens (triangle)
            ImGui::GetWindowDrawList()->AddTriangleFilled(
                ImVec2(cx + 10*iconScale, cy - 8*iconScale), ImVec2(cx + 25*iconScale, cy), ImVec2(cx + 10*iconScale, cy + 8*iconScale), iconColor);
            // Draw viewfinder
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(cx - 15*iconScale, cy - 18*iconScale), ImVec2(cx, cy - 10*iconScale), iconColor, 2.0f);
        } else if (kind == vivid::OutputKind::Light) {
            // Light operator - draw light bulb icon
            ImGui::Dummy(ImVec2(thumbW, thumbH * 0.9f));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(60, 50, 25, 255), 4.0f);

            // Draw bulb (circle) - scale icon with thumbnail
            float cx = (min.x + max.x) * 0.5f;
            float cy = (min.y + max.y) * 0.5f - 3 * thumbScale;
            float iconScale = thumbScale;
            ImU32 iconColor = IM_COL32(255, 220, 100, 255);
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cx, cy), 12 * iconScale, iconColor);
            // Draw base
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(cx - 6*iconScale, cy + 10*iconScale), ImVec2(cx + 6*iconScale, cy + 18*iconScale), IM_COL32(180, 180, 180, 255), 2.0f);
            // Draw rays
            ImU32 rayColor = IM_COL32(255, 240, 150, 180);
            for (int i = 0; i < 8; i++) {
                float angle = i * 3.14159f / 4;
                float r1 = 15 * iconScale, r2 = 22 * iconScale;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(cx + r1 * cos(angle), cy + r1 * sin(angle)),
                    ImVec2(cx + r2 * cos(angle), cy + r2 * sin(angle)),
                    rayColor, 2.0f);
            }
        } else if (kind == vivid::OutputKind::Audio) {
            // Audio operator - draw specialized visualization based on type
            ImGui::Dummy(ImVec2(thumbW, thumbH * 0.9f));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();

            AudioOperator* audioOp = static_cast<AudioOperator*>(info.op);

            float cx = (min.x + max.x) * 0.5f;
            float cy = (min.y + max.y) * 0.5f;
            float height = max.y - min.y;
            float width = max.x - min.x;

            // Check for specific drum synth types
            if (auto* kick = dynamic_cast<audio::Kick*>(audioOp)) {
                // Kick drum - pitch sweep + amplitude envelope
                dl->AddRectFilled(min, max, IM_COL32(60, 25, 15, 255), 4.0f);

                float ampEnv = kick->ampEnvelope();
                float pitchEnv = kick->pitchEnvelope();

                // Draw pitch trajectory (curved line from top-left to bottom)
                float startY = min.y + height * 0.2f;
                float endY = max.y - height * 0.15f;
                float curveX = min.x + width * 0.3f;

                // Pitch curve (shows high-to-low sweep)
                ImU32 pitchColor = IM_COL32(255, 140, 50, 180);
                for (int i = 0; i < 20; i++) {
                    float t1 = i / 20.0f;
                    float t2 = (i + 1) / 20.0f;
                    // Exponential decay curve
                    float y1 = startY + (endY - startY) * (1.0f - std::exp(-t1 * 4.0f));
                    float y2 = startY + (endY - startY) * (1.0f - std::exp(-t2 * 4.0f));
                    float x1 = min.x + width * 0.15f + t1 * width * 0.7f;
                    float x2 = min.x + width * 0.15f + t2 * width * 0.7f;
                    dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), pitchColor, 2.0f);
                }

                // Amplitude envelope bar (vertical on right side)
                float barW = width * 0.15f;
                float barH = height * 0.7f * ampEnv;
                ImU32 ampColor = IM_COL32(255, 100 + (int)(155 * ampEnv), 50, 255);
                dl->AddRectFilled(
                    ImVec2(max.x - barW - 4, max.y - 4 - barH),
                    ImVec2(max.x - 4, max.y - 4),
                    ampColor, 2.0f);

            } else if (auto* snare = dynamic_cast<audio::Snare*>(audioOp)) {
                // Snare - dual envelope (tone bottom, noise top)
                dl->AddRectFilled(min, max, IM_COL32(45, 40, 50, 255), 4.0f);

                float toneEnv = snare->toneEnvelope();
                float noiseEnv = snare->noiseEnvelope();
                float halfH = height * 0.45f;

                // Tone envelope (bottom half, warm color)
                float toneBarH = halfH * toneEnv;
                ImU32 toneColor = IM_COL32(200, 140, 80, 255);
                dl->AddRectFilled(
                    ImVec2(min.x + 4, cy + 2),
                    ImVec2(max.x - 4, cy + 2 + toneBarH),
                    toneColor, 2.0f);

                // Noise envelope (top half, white/gray)
                float noiseBarH = halfH * noiseEnv;
                ImU32 noiseColor = IM_COL32(220, 220, 230, 255);
                dl->AddRectFilled(
                    ImVec2(min.x + 4, cy - 2 - noiseBarH),
                    ImVec2(max.x - 4, cy - 2),
                    noiseColor, 2.0f);

                // Divider line
                dl->AddLine(ImVec2(min.x + 4, cy), ImVec2(max.x - 4, cy),
                    IM_COL32(100, 100, 110, 150), 1.0f);

            } else if (auto* hihat = dynamic_cast<audio::HiHat*>(audioOp)) {
                // Hi-hat - shimmer/sparkle visualization
                dl->AddRectFilled(min, max, IM_COL32(30, 45, 50, 255), 4.0f);

                float env = hihat->envelope();

                // Draw metallic shimmer lines radiating from center
                int numRays = 8;
                float maxRadius = std::min(width, height) * 0.4f;
                float radius = maxRadius * env;

                for (int i = 0; i < numRays; i++) {
                    float angle = (i / (float)numRays) * 3.14159f * 2.0f;
                    float r1 = radius * 0.3f;
                    float r2 = radius;
                    ImU32 rayColor = IM_COL32(
                        150 + (int)(105 * env),
                        200 + (int)(55 * env),
                        220, (int)(200 * env));
                    dl->AddLine(
                        ImVec2(cx + r1 * std::cos(angle), cy + r1 * std::sin(angle)),
                        ImVec2(cx + r2 * std::cos(angle), cy + r2 * std::sin(angle)),
                        rayColor, 1.5f + env);
                }

                // Center dot
                dl->AddCircleFilled(ImVec2(cx, cy), 3.0f + env * 4.0f,
                    IM_COL32(200, 240, 255, (int)(255 * env)));

            } else if (auto* clap = dynamic_cast<audio::Clap*>(audioOp)) {
                // Clap - multiple burst visualization
                dl->AddRectFilled(min, max, IM_COL32(50, 30, 45, 255), 4.0f);

                // Draw 4 burst bars
                float barW = (width - 20) / 4.0f;
                float maxBarH = height * 0.75f;

                for (int i = 0; i < 4; i++) {
                    float burstEnv = clap->burstEnvelope(i);
                    float barH = maxBarH * burstEnv;
                    float barX = min.x + 6 + i * (barW + 2);

                    // Pink/magenta gradient based on intensity
                    ImU32 burstColor = IM_COL32(
                        200 + (int)(55 * burstEnv),
                        80 + (int)(100 * burstEnv),
                        180, 255);

                    dl->AddRectFilled(
                        ImVec2(barX, max.y - 4 - barH),
                        ImVec2(barX + barW - 2, max.y - 4),
                        burstColor, 2.0f);
                }

            } else {
                // Generic audio - draw waveform
                dl->AddRectFilled(min, max, IM_COL32(50, 30, 60, 255), 4.0f);

                const AudioBuffer* buf = audioOp->outputBuffer();
                float startX = min.x + 4.0f;
                float waveWidth = width - 8.0f;

                ImU32 waveColor = IM_COL32(180, 140, 220, 255);
                ImU32 waveColorDim = IM_COL32(120, 80, 160, 200);

                if (buf && buf->isValid() && buf->sampleCount() > 0) {
                    constexpr int NUM_POINTS = 48;
                    uint32_t step = std::max(1u, buf->frameCount / NUM_POINTS);

                    float prevX = startX;
                    float prevY = cy;

                    for (int i = 0; i < NUM_POINTS && i * step < buf->frameCount; i++) {
                        uint32_t frameIdx = i * step;
                        float sample = (buf->samples[frameIdx * 2] + buf->samples[frameIdx * 2 + 1]) * 0.5f;
                        sample = std::max(-1.0f, std::min(1.0f, sample));

                        float x = startX + (waveWidth * i / (NUM_POINTS - 1));
                        float y = cy - sample * height * 0.4f;

                        if (i > 0) {
                            dl->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), waveColor, 1.5f);
                        }
                        prevX = x;
                        prevY = y;
                    }
                } else {
                    for (int i = 0; i < 3; i++) {
                        float x1 = startX + waveWidth * i / 3.0f;
                        float x2 = startX + waveWidth * (i + 1) / 3.0f;
                        float xMid = (x1 + x2) * 0.5f;
                        float yOffset = (i == 1) ? height * 0.15f : -height * 0.1f;
                        dl->AddBezierQuadratic(
                            ImVec2(x1, cy), ImVec2(xMid, cy + yOffset), ImVec2(x2, cy),
                            waveColorDim, 1.5f);
                    }
                }
            }
        } else if (kind == vivid::OutputKind::AudioValue) {
            // Audio analysis - draw based on specific analyzer type
            ImGui::Dummy(ImVec2(thumbW, thumbH * 0.9f));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Dark purple background
            dl->AddRectFilled(min, max, IM_COL32(40, 30, 50, 255), 4.0f);

            float width = max.x - min.x - 8.0f;
            float height = max.y - min.y - 8.0f;
            float startX = min.x + 4.0f;
            float startY = min.y + 4.0f;

            if (auto* levels = dynamic_cast<vivid::audio::Levels*>(info.op)) {
                // Levels: VU meter with two bars (RMS and Peak)
                float rms = levels->rms();
                float peak = levels->peak();

                float barWidth = width * 0.35f;
                float gap = width * 0.1f;
                float leftX = startX + width * 0.1f;
                float rightX = leftX + barWidth + gap;

                // Draw RMS bar (left) with gradient
                float rmsHeight = rms * height;
                for (int i = 0; i < static_cast<int>(rmsHeight); i++) {
                    float t = static_cast<float>(i) / height;
                    ImU32 col;
                    if (t < 0.5f) col = IM_COL32(80, 180, 80, 255);       // Green
                    else if (t < 0.8f) col = IM_COL32(200, 180, 60, 255); // Yellow
                    else col = IM_COL32(200, 80, 80, 255);                 // Red
                    float y = max.y - 4.0f - i;
                    dl->AddLine(ImVec2(leftX, y), ImVec2(leftX + barWidth, y), col);
                }
                dl->AddRect(ImVec2(leftX, startY), ImVec2(leftX + barWidth, max.y - 4.0f),
                           IM_COL32(80, 80, 100, 150), 2.0f);

                // Draw Peak bar (right)
                float peakHeight = peak * height;
                for (int i = 0; i < static_cast<int>(peakHeight); i++) {
                    float t = static_cast<float>(i) / height;
                    ImU32 col;
                    if (t < 0.5f) col = IM_COL32(80, 180, 80, 255);
                    else if (t < 0.8f) col = IM_COL32(200, 180, 60, 255);
                    else col = IM_COL32(200, 80, 80, 255);
                    float y = max.y - 4.0f - i;
                    dl->AddLine(ImVec2(rightX, y), ImVec2(rightX + barWidth, y), col);
                }
                dl->AddRect(ImVec2(rightX, startY), ImVec2(rightX + barWidth, max.y - 4.0f),
                           IM_COL32(80, 80, 100, 150), 2.0f);

            } else if (auto* fft = dynamic_cast<vivid::audio::FFT*>(info.op)) {
                // FFT: Spectrum analyzer bars
                const float* spectrum = fft->spectrum();
                int binCount = fft->binCount();
                constexpr int NUM_BARS = 24;

                float barW = width / NUM_BARS - 1.0f;

                for (int i = 0; i < NUM_BARS; i++) {
                    // Sample from spectrum (logarithmic distribution)
                    int binIdx = static_cast<int>(std::pow(static_cast<float>(i + 1) / NUM_BARS, 2.0f) * binCount * 0.5f);
                    binIdx = std::min(binIdx, binCount - 1);
                    float mag = spectrum ? spectrum[binIdx] * 3.0f : 0.0f;  // Scale up
                    mag = std::min(mag, 1.0f);

                    float barH = mag * height;
                    float x = startX + i * (barW + 1.0f);
                    float y = max.y - 4.0f - barH;

                    // Color gradient: blue -> purple -> pink
                    float t = static_cast<float>(i) / NUM_BARS;
                    uint8_t r = static_cast<uint8_t>(80 + t * 140);
                    uint8_t g = static_cast<uint8_t>(80 - t * 40);
                    uint8_t b = static_cast<uint8_t>(180 - t * 40);
                    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + barW, max.y - 4.0f),
                                     IM_COL32(r, g, b, 220), 1.0f);
                }

            } else if (auto* bands = dynamic_cast<vivid::audio::BandSplit*>(info.op)) {
                // BandSplit: 6 frequency band bars
                float bandValues[6] = {
                    bands->subBass(), bands->bass(), bands->lowMid(),
                    bands->mid(), bands->highMid(), bands->high()
                };
                ImU32 bandColors[6] = {
                    IM_COL32(120, 60, 160, 255),  // SubBass - deep purple
                    IM_COL32(60, 100, 200, 255),  // Bass - blue
                    IM_COL32(60, 180, 180, 255),  // LowMid - cyan
                    IM_COL32(100, 200, 100, 255), // Mid - green
                    IM_COL32(220, 200, 60, 255),  // HighMid - yellow
                    IM_COL32(220, 100, 80, 255)   // High - red/orange
                };

                float barW = width / 6.0f - 2.0f;
                for (int i = 0; i < 6; i++) {
                    float barH = bandValues[i] * 2.0f * height;  // Scale up
                    barH = std::min(barH, height);
                    float x = startX + i * (barW + 2.0f) + 1.0f;
                    float y = max.y - 4.0f - barH;
                    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + barW, max.y - 4.0f),
                                     bandColors[i], 2.0f);
                }

            } else if (auto* beat = dynamic_cast<vivid::audio::BeatDetect*>(info.op)) {
                // BeatDetect: Pulsing circle
                float cx = (min.x + max.x) * 0.5f;
                float cy = (min.y + max.y) * 0.5f;
                float maxRadius = std::min(width, height) * 0.4f;

                float energy = beat->energy();
                float intensity = beat->intensity();
                bool isBeat = beat->beat();

                // Outer ring (intensity)
                float outerR = maxRadius * (0.6f + intensity * 0.4f);
                dl->AddCircle(ImVec2(cx, cy), outerR,
                             IM_COL32(100, 150, 200, static_cast<int>(100 + intensity * 155)), 24, 2.0f);

                // Inner circle (energy)
                float innerR = maxRadius * 0.3f * (0.5f + energy * 1.5f);
                innerR = std::min(innerR, outerR - 2.0f);

                // Flash white on beat
                ImU32 fillColor = isBeat ?
                    IM_COL32(255, 255, 255, 255) :
                    IM_COL32(80 + static_cast<int>(energy * 100),
                             120 + static_cast<int>(energy * 80),
                             180, 220);

                dl->AddCircleFilled(ImVec2(cx, cy), innerR, fillColor, 24);

            } else {
                // Unknown AudioValue type
                dl->AddText(ImVec2(min.x + 35, min.y + 18), IM_COL32(150, 100, 180, 255), "AV");
            }
        } else {
            // Unknown type
            ImGui::Dummy(ImVec2(100, 40));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(40, 40, 50, 255), 4.0f);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(min.x + 20, min.y + 12), IM_COL32(100, 100, 120, 255), "???");
        }

        // Output pin
        ImNodes::BeginOutputAttribute(outputAttrId(nodeId));
        ImGui::Text("out");
        ImNodes::EndOutputAttribute();

        ImNodes::EndNode();

        // Pop color styles if we pushed them
        if (pushedStyle) {
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }
    }

    // Render the Screen output node (special node showing final output destination)
    vivid::Operator* outputOp = nullptr;
    if (ctx.hasChain()) {
        outputOp = ctx.chain().getOutput();
    }

    if (outputOp && m_opToNodeId.count(outputOp)) {
        // Position Screen node to the right of the output operator
        if (!m_nodePositioned[SCREEN_NODE_ID]) {
            int outputNodeId = m_opToNodeId[outputOp];
            ImVec2 outputPos = ImNodes::GetNodeGridSpacePos(outputNodeId);
            ImNodes::SetNodeGridSpacePos(SCREEN_NODE_ID, ImVec2(outputPos.x + 280.0f, outputPos.y));
            m_nodePositioned[SCREEN_NODE_ID] = true;
        }

        // Green color for Screen node
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(40, 120, 60, 255));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(50, 150, 75, 255));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(60, 180, 90, 255));

        ImNodes::BeginNode(SCREEN_NODE_ID);

        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("Screen");
        ImNodes::EndNodeTitleBar();

        // Input pin for the texture
        ImNodes::BeginInputAttribute(inputAttrId(SCREEN_NODE_ID, 0));
        ImGui::Text("display");
        ImNodes::EndInputAttribute();

        ImNodes::EndNode();

        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    // Render the Speakers audio output node (if audio output is designated)
    vivid::Operator* audioOutputOp = nullptr;
    if (ctx.hasChain()) {
        audioOutputOp = ctx.chain().getAudioOutput();
    }

    if (audioOutputOp && m_opToNodeId.count(audioOutputOp)) {
        // Position Speakers node to the right of the audio output operator
        if (!m_nodePositioned[SPEAKERS_NODE_ID]) {
            int audioOutputNodeId = m_opToNodeId[audioOutputOp];
            ImVec2 audioOutputPos = ImNodes::GetNodeGridSpacePos(audioOutputNodeId);
            ImNodes::SetNodeGridSpacePos(SPEAKERS_NODE_ID, ImVec2(audioOutputPos.x + 280.0f, audioOutputPos.y));
            m_nodePositioned[SPEAKERS_NODE_ID] = true;
        }

        // Purple color for Speakers node (matches audio operators)
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(100, 60, 120, 255));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(125, 75, 150, 255));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(150, 90, 180, 255));

        ImNodes::BeginNode(SPEAKERS_NODE_ID);

        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("Speakers");
        ImNodes::EndNodeTitleBar();

        // Input pin for the audio
        ImNodes::BeginInputAttribute(inputAttrId(SPEAKERS_NODE_ID, 0));
        ImGui::Text("audio");
        ImNodes::EndInputAttribute();

        ImNodes::EndNode();

        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    // Render links
    int linkId = 0;
    for (size_t i = 0; i < operators.size(); ++i) {
        const vivid::OperatorInfo& info = operators[i];
        if (!info.op) continue;

        int destNodeId = static_cast<int>(i);

        for (size_t j = 0; j < info.op->inputCount(); ++j) {
            vivid::Operator* inputOp = info.op->getInput(static_cast<int>(j));
            if (inputOp && m_opToNodeId.count(inputOp)) {
                int sourceNodeId = m_opToNodeId[inputOp];
                ImNodes::Link(linkId++,
                    outputAttrId(sourceNodeId),
                    inputAttrId(destNodeId, static_cast<int>(j)));
            }
        }
    }

    // Link from output operator to Screen node
    if (outputOp && m_opToNodeId.count(outputOp)) {
        int outputNodeId = m_opToNodeId[outputOp];
        ImNodes::Link(linkId++,
            outputAttrId(outputNodeId),
            inputAttrId(SCREEN_NODE_ID, 0));
    }

    // Link from audio output operator to Speakers node
    if (audioOutputOp && m_opToNodeId.count(audioOutputOp)) {
        int audioOutputNodeId = m_opToNodeId[audioOutputOp];
        ImNodes::Link(linkId++,
            outputAttrId(audioOutputNodeId),
            inputAttrId(SPEAKERS_NODE_ID, 0));
    }

    // Handle pending editor selection (from VSCode)
    if (!m_pendingEditorSelection.empty()) {
        // Find the operator by name and select its node
        for (const auto& info : operators) {
            if (info.name == m_pendingEditorSelection && info.op && m_opToNodeId.count(info.op)) {
                int nodeId = m_opToNodeId[info.op];
                ImNodes::ClearNodeSelection();
                ImNodes::SelectNode(nodeId);
                // Center view on the selected node
                ImNodes::EditorContextMoveToNode(nodeId);
                break;
            }
        }
        m_pendingEditorSelection.clear();
    }

    ImNodes::EndNodeEditor();

    // Update selection state from ImNodes
    updateSelection(operators);

    // Show tooltip with resource stats when hovering over a node
    int hoveredNodeId = -1;
    if (ImNodes::IsNodeHovered(&hoveredNodeId) && hoveredNodeId >= 0 &&
        hoveredNodeId != SCREEN_NODE_ID && hoveredNodeId != SPEAKERS_NODE_ID) {
        // Find the operator for this node
        for (const auto& info : operators) {
            if (info.op && m_opToNodeId.count(info.op) && m_opToNodeId[info.op] == hoveredNodeId) {
                ImGui::BeginTooltip();

                // Operator type and name
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s", info.op->name().c_str());
                if (info.name != info.op->name()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "(%s)", info.name.c_str());
                }

                ImGui::Separator();

                // Output type
                vivid::OutputKind kind = info.op->outputKind();
                const char* kindStr = "Unknown";
                switch (kind) {
                    case vivid::OutputKind::Texture: kindStr = "Texture"; break;
                    case vivid::OutputKind::Geometry: kindStr = "Geometry"; break;
                    case vivid::OutputKind::Audio: kindStr = "Audio"; break;
                    case vivid::OutputKind::AudioValue: kindStr = "Audio Value"; break;
                    case vivid::OutputKind::Value: kindStr = "Value"; break;
                    case vivid::OutputKind::ValueArray: kindStr = "Value Array"; break;
                    case vivid::OutputKind::Camera: kindStr = "Camera"; break;
                    case vivid::OutputKind::Light: kindStr = "Light"; break;
                }
                ImGui::Text("Output: %s", kindStr);

                // Resource info based on type
                if (kind == vivid::OutputKind::Texture) {
                    WGPUTexture tex = info.op->outputTexture();
                    if (tex) {
                        uint32_t w = wgpuTextureGetWidth(tex);
                        uint32_t h = wgpuTextureGetHeight(tex);
                        // RGBA16Float = 8 bytes per pixel
                        size_t memBytes = w * h * 8;
                        ImGui::Text("Size: %ux%u", w, h);
                        ImGui::Text("Memory: ~%.1f MB", memBytes / (1024.0f * 1024.0f));
                    } else {
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No texture");
                    }
                } else if (kind == vivid::OutputKind::Geometry) {
                    if (auto* meshOp = dynamic_cast<render3d::MeshOperator*>(info.op)) {
                        render3d::Mesh* mesh = meshOp->outputMesh();
                        if (mesh) {
                            ImGui::Text("Vertices: %u", mesh->vertexCount());
                            ImGui::Text("Triangles: %u", mesh->indexCount() / 3);
                        }
                    } else if (auto* composer = dynamic_cast<render3d::SceneComposer*>(info.op)) {
                        render3d::Scene& scene = composer->outputScene();
                        ImGui::Text("Objects: %zu", scene.objects().size());
                    }
                } else if (kind == vivid::OutputKind::Audio) {
                    AudioOperator* audioOp = static_cast<AudioOperator*>(info.op);
                    const AudioBuffer* buf = audioOp->outputBuffer();
                    if (buf && buf->isValid()) {
                        ImGui::Text("Channels: %u", buf->channels);
                        ImGui::Text("Frames: %u", buf->frameCount);
                    }
                }

                // Bypass status
                if (info.op->isBypassed()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "BYPASSED");
                }

                ImGui::EndTooltip();
                break;
            }
        }
    }

    // Handle blank space click to deselect
    int hoveredNodeCheck = -1;
    bool isNodeHovered = ImNodes::IsNodeHovered(&hoveredNodeCheck);
    int hoveredLink = -1;
    bool isLinkHovered = ImNodes::IsLinkHovered(&hoveredLink);
    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !isNodeHovered && !isLinkHovered) {
        ImNodes::ClearNodeSelection();
        clearSelection();
    }

    // Handle double-click on nodes to enter solo mode
    int hoveredNode = -1;
    if (ImNodes::IsNodeHovered(&hoveredNode) && ImGui::IsMouseDoubleClicked(0)) {
        // Find the operator for this node
        for (const auto& info : operators) {
            if (info.op && m_opToNodeId.count(info.op) && m_opToNodeId[info.op] == hoveredNode) {
                enterSoloMode(info.op, info.name);
                break;
            }
        }
    }

    ImGui::End();

    // Render debug values panel if there are any
    renderDebugPanel(ctx);
}

// -------------------------------------------------------------------------
// Debug Values Panel
// -------------------------------------------------------------------------

void ChainVisualizer::renderDebugPanel(vivid::Context& ctx) {
    const auto& debugValues = ctx.debugValues();
    if (debugValues.empty()) return;

    // Position in bottom-left corner
    ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 180), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.8f);

    if (ImGui::Begin("Debug Values", nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) {
        for (const auto& [name, dv] : debugValues) {
            // Skip values that weren't updated this frame (grayed out)
            if (!dv.updatedThisFrame) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            }

            // Name column (fixed width)
            ImGui::Text("%-12s", name.c_str());
            ImGui::SameLine();

            // Sparkline graph
            if (!dv.history.empty()) {
                // Convert deque to vector for PlotLines
                std::vector<float> historyVec(dv.history.begin(), dv.history.end());

                // Find min/max for scaling
                float minVal = *std::min_element(historyVec.begin(), historyVec.end());
                float maxVal = *std::max_element(historyVec.begin(), historyVec.end());

                // Ensure some range even for constant values
                if (maxVal - minVal < 0.001f) {
                    minVal -= 0.5f;
                    maxVal += 0.5f;
                }

                // Draw sparkline (use ##name for unique ID)
                std::string plotId = "##" + name;
                ImGui::PlotLines(plotId.c_str(), historyVec.data(), static_cast<int>(historyVec.size()),
                               0, nullptr, minVal, maxVal, ImVec2(120, 20));
            }

            ImGui::SameLine();

            // Current value
            ImGui::Text("%7.3f", dv.current);

            if (!dv.updatedThisFrame) {
                ImGui::PopStyleColor();
            }
        }
    }
    ImGui::End();
}

// -------------------------------------------------------------------------
// Video Recording
// -------------------------------------------------------------------------

void ChainVisualizer::startRecording(ExportCodec codec, vivid::Context& ctx) {
    // Generate output path in the project directory (same as chain.cpp)
    std::string projectDir = ".";
    const std::string& chainPath = ctx.chainPath();
    if (!chainPath.empty()) {
        size_t lastSlash = chainPath.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            projectDir = chainPath.substr(0, lastSlash);
        }
    }
    std::string outputPath = VideoExporter::generateOutputPath(projectDir, codec);

    // Get output resolution from the actual output texture
    int width = ctx.width();
    int height = ctx.height();

    WGPUTexture outputTex = ctx.chain().outputTexture();
    if (outputTex) {
        width = static_cast<int>(wgpuTextureGetWidth(outputTex));
        height = static_cast<int>(wgpuTextureGetHeight(outputTex));
    }

    float fps = 60.0f;  // TODO: Get from context if available

    // Check if chain has audio output
    bool hasAudio = ctx.chain().getAudioOutput() != nullptr;

    bool started = false;
    if (hasAudio) {
        // Start with audio muxing
        started = m_exporter.startWithAudio(outputPath, width, height, fps, codec, 48000, 2);
        if (started) {
            // Set recording mode so audio operators generate correct amount per frame
            ctx.setRecordingMode(true, fps);
            printf("[ChainVisualizer] Recording started with audio: %s\n", outputPath.c_str());
        }
    } else {
        started = m_exporter.start(outputPath, width, height, fps, codec);
        if (started) {
            ctx.setRecordingMode(true, fps);
            printf("[ChainVisualizer] Recording started: %s\n", outputPath.c_str());
        }
    }

    if (!started) {
        printf("[ChainVisualizer] Failed to start recording: %s\n", m_exporter.error().c_str());
    }
}

void ChainVisualizer::stopRecording(vivid::Context& ctx) {
    m_exporter.stop();
    ctx.setRecordingMode(false);
}

void ChainVisualizer::saveSnapshot(WGPUDevice device, WGPUQueue queue, WGPUTexture texture, vivid::Context& ctx) {
    m_snapshotRequested = false;

    if (!texture) {
        printf("[ChainVisualizer] Snapshot failed: no output texture\n");
        return;
    }

    // Generate output filename in project directory
    std::string projectDir = ".";
    const std::string& chainPath = ctx.chainPath();
    if (!chainPath.empty()) {
        fs::path p(chainPath);
        if (p.has_parent_path()) {
            projectDir = p.parent_path().string();
        }
    }

    // Find next available snapshot number
    int snapshotNum = 1;
    std::string outputPath;
    do {
        outputPath = projectDir + "/snapshot_" + std::to_string(snapshotNum) + ".png";
        snapshotNum++;
    } while (fs::exists(outputPath) && snapshotNum < 10000);

    // Delegate to VideoExporter's static snapshot utility
    if (VideoExporter::saveSnapshot(device, queue, texture, outputPath)) {
        printf("[ChainVisualizer] Snapshot saved: %s\n", outputPath.c_str());
    } else {
        printf("[ChainVisualizer] Snapshot failed: couldn't save PNG\n");
    }
}

void ChainVisualizer::updateSelection(const std::vector<vivid::OperatorInfo>& operators) {
    int numSelected = ImNodes::NumSelectedNodes();
    if (numSelected == 1) {
        int selectedId;
        ImNodes::GetSelectedNodes(&selectedId);

        // Only update if selection changed
        if (selectedId != m_selectedNodeId) {
            m_selectedNodeId = selectedId;

            // Find the operator for this node
            m_selectedOp = nullptr;
            m_selectedOpName.clear();
            for (const auto& info : operators) {
                if (info.op && m_opToNodeId.count(info.op) && m_opToNodeId[info.op] == selectedId) {
                    m_selectedOp = info.op;
                    m_selectedOpName = info.name;
                    break;
                }
            }
        }
    } else if (numSelected == 0 || numSelected > 1) {
        // Clear selection if none or multiple selected
        if (m_selectedOp != nullptr) {
            clearSelection();
        }
    }
}

void ChainVisualizer::clearSelection() {
    m_selectedNodeId = -1;
    m_selectedOp = nullptr;
    m_selectedOpName.clear();
}

void ChainVisualizer::setFocusedNode(const std::string& operatorName) {
    m_focusedOperatorName = operatorName;
    m_focusedModeActive = !operatorName.empty();
}

void ChainVisualizer::clearFocusedNode() {
    m_focusedOperatorName.clear();
    m_focusedModeActive = false;
}

bool ChainVisualizer::isFocused(const std::string& operatorName) const {
    return m_focusedModeActive && m_focusedOperatorName == operatorName;
}

} // namespace vivid::imgui
