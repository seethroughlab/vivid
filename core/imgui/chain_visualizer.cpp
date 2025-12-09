// Vivid Chain Visualizer Implementation
// Shows registered operators as nodes with connections

#include "chain_visualizer.h"
#include <imgui.h>
#include <imnodes.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>

namespace vivid::imgui {

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

void ChainVisualizer::loadAndApplySidecar(vivid::Context& ctx) {
    const std::string& chainPath = ctx.chainPath();
    if (chainPath.empty()) return;

    // Load sidecar if not already loaded
    if (m_sidecarPath.empty()) {
        loadSidecar(chainPath);
    }

    // Apply overrides to registered operators
    if (!m_paramOverrides.empty()) {
        applyOverrides(ctx.registeredOperators());
    }
}

// Estimate node height based on content
float ChainVisualizer::estimateNodeHeight(const vivid::OperatorInfo& info) const {
    float height = 0.0f;

    // Title bar
    height += 24.0f;

    // Type name (if different from registered name)
    if (info.op && info.op->name() != info.name) {
        height += 18.0f;
    }

    // Parameters (each line ~18px)
    if (info.op) {
        auto params = info.op->params();
        height += params.size() * 18.0f;
        if (!params.empty()) {
            height += 8.0f;  // Separator spacing
        }
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
    // Initialize renderer if needed
    if (!preview.renderer) {
        preview.renderer = std::make_unique<render3d::Render3D>();
        preview.renderer->resolution(100, 56)
            .shadingMode(render3d::ShadingMode::Flat)
            .clearColor(0.12f, 0.14f, 0.18f)
            .ambient(0.3f)
            .lightDirection(glm::normalize(glm::vec3(1, 2, 1)));
        preview.renderer->init(ctx);
    }

    // Update rotation
    preview.rotationAngle += dt * 0.8f;  // ~0.8 rad/sec rotation

    // Rebuild scene if mesh changed
    if (mesh != preview.lastMesh) {
        preview.scene.clear();
        if (mesh) {
            // Internal use - suppress deprecation warning (this is internal preview code)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            preview.scene.add(*mesh, glm::mat4(1.0f), glm::vec4(0.7f, 0.85f, 1.0f, 1.0f));
#pragma clang diagnostic pop
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
        preview.camera.lookAt(
            glm::vec3(distance * 0.7f, distance * 0.5f, distance * 0.7f),
            center
        ).fov(45.0f).nearPlane(0.01f).farPlane(100.0f);
    }

    // Render (internal use - suppress deprecation warning)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    preview.renderer->scene(preview.scene).camera(preview.camera);
#pragma clang diagnostic pop
    preview.renderer->process(ctx);
}

void ChainVisualizer::updateScenePreview(
    GeometryPreview& preview,
    render3d::SceneComposer* composer,
    vivid::Context& ctx,
    float dt
) {
    // Initialize renderer if needed
    if (!preview.renderer) {
        preview.renderer = std::make_unique<render3d::Render3D>();
        preview.renderer->resolution(100, 56)
            .shadingMode(render3d::ShadingMode::Flat)
            .clearColor(0.12f, 0.14f, 0.18f)
            .ambient(0.3f)
            .lightDirection(glm::normalize(glm::vec3(1, 2, 1)));
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
    preview.camera.lookAt(
        glm::vec3(camX, center.y + distance * 0.4f, camZ),
        center
    ).fov(45.0f).nearPlane(0.01f).farPlane(100.0f);

    // Render (internal use - suppress deprecation warning)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    preview.renderer->scene(scene).camera(preview.camera);
#pragma clang diagnostic pop
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

        // Initialize or resize renderer
        if (!m_soloGeometryRenderer) {
            m_soloGeometryRenderer = std::make_unique<render3d::Render3D>();
            m_soloGeometryRenderer->shadingMode(render3d::ShadingMode::Flat)
                .clearColor(0.08f, 0.1f, 0.14f)
                .ambient(0.3f)
                .lightDirection(glm::normalize(glm::vec3(1, 2, 1)));
            m_soloGeometryRenderer->init(ctx);
        }

        // Resize to match viewport
        m_soloGeometryRenderer->resolution(input.width, input.height);

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
                render3d::Camera3D camera;
                camera.lookAt(
                    glm::vec3(camX, center.y + distance * 0.4f, camZ),
                    center
                ).fov(45.0f).nearPlane(0.01f).farPlane(1000.0f);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                m_soloGeometryRenderer->scene(scene).camera(camera);
#pragma clang diagnostic pop
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
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                scene.add(*mesh, transform, glm::vec4(0.7f, 0.85f, 1.0f, 1.0f));
#pragma clang diagnostic pop

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

                render3d::Camera3D camera;
                camera.lookAt(
                    glm::vec3(distance * 0.7f, distance * 0.5f, distance * 0.7f),
                    center
                ).fov(45.0f).nearPlane(0.01f).farPlane(100.0f);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                m_soloGeometryRenderer->scene(scene).camera(camera);
#pragma clang diagnostic pop
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

    // Load sidecar file if chain path changed
    const std::string& chainPath = ctx.chainPath();
    if (!chainPath.empty() && m_sidecarPath.empty()) {
        loadSidecar(chainPath);
        if (!m_paramOverrides.empty()) {
            applyOverrides(operators);
        }
    }

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
                stopRecording();
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

        // Show parameters with editable sliders if operator supports it
        auto params = info.op->params();
        if (!params.empty()) {
            // Draw a separator line within the node
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(pos.x, pos.y + 2),
                ImVec2(pos.x + 100, pos.y + 2),
                IM_COL32(80, 80, 90, 255), 1.0f);
            ImGui::Dummy(ImVec2(0, 6));

            ImGui::PushItemWidth(80);  // Compact sliders for nodes

            for (const auto& p : params) {
                // Get current value (or use default)
                float currentVal[4] = {p.defaultVal[0], p.defaultVal[1], p.defaultVal[2], p.defaultVal[3]};
                info.op->getParam(p.name, currentVal);

                // Create unique ID for this param
                std::string paramId = info.name + "." + p.name;
                ImGui::PushID(paramId.c_str());

                bool changed = false;
                switch (p.type) {
                    case vivid::ParamType::Float:
                        changed = ImGui::SliderFloat(p.name.c_str(), &currentVal[0], p.minVal, p.maxVal);
                        break;
                    case vivid::ParamType::Int:
                        {
                            int intVal = static_cast<int>(currentVal[0]);
                            if (ImGui::SliderInt(p.name.c_str(), &intVal,
                                    static_cast<int>(p.minVal), static_cast<int>(p.maxVal))) {
                                currentVal[0] = static_cast<float>(intVal);
                                changed = true;
                            }
                        }
                        break;
                    case vivid::ParamType::Bool:
                        {
                            bool boolVal = currentVal[0] > 0.5f;
                            if (ImGui::Checkbox(p.name.c_str(), &boolVal)) {
                                currentVal[0] = boolVal ? 1.0f : 0.0f;
                                changed = true;
                            }
                        }
                        break;
                    case vivid::ParamType::Vec2:
                        changed = ImGui::SliderFloat2(p.name.c_str(), currentVal, p.minVal, p.maxVal);
                        break;
                    case vivid::ParamType::Vec3:
                        changed = ImGui::SliderFloat3(p.name.c_str(), currentVal, p.minVal, p.maxVal);
                        break;
                    case vivid::ParamType::Color:
                        changed = ImGui::ColorEdit3(p.name.c_str(), currentVal);
                        break;
                    case vivid::ParamType::Vec4:
                        changed = ImGui::SliderFloat4(p.name.c_str(), currentVal, p.minVal, p.maxVal);
                        break;
                    case vivid::ParamType::String:
                        ImGui::Text("%s", p.name.c_str());
                        break;
                    default:
                        ImGui::Text("%s", p.name.c_str());
                        break;
                }

                if (changed) {
                    // Apply change to operator
                    if (info.op->setParam(p.name, currentVal)) {
                        // Store in sidecar
                        std::string key = makeParamKey(info.name, p.name);
                        m_paramOverrides[key] = {currentVal[0], currentVal[1], currentVal[2], currentVal[3]};
                        m_sidecarDirty = true;
                    }
                }

                ImGui::PopID();
            }
            ImGui::PopItemWidth();
        }

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

        if (kind == vivid::OutputKind::Texture) {
            WGPUTextureView view = info.op->outputView();
            if (view) {
                // ImGui WebGPU backend accepts WGPUTextureView as ImTextureID
                ImTextureID texId = reinterpret_cast<ImTextureID>(view);
                ImGui::Image(texId, ImVec2(100, 56));  // 16:9 aspect ratio
            } else {
                // Texture operator but no view yet
                ImGui::Dummy(ImVec2(100, 40));
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
                        ImGui::Image(texId, ImVec2(100, 56));
                    } else {
                        ImGui::Dummy(ImVec2(100, 56));
                    }
                } else {
                    // Empty scene placeholder
                    ImGui::Dummy(ImVec2(100, 56));
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
                        ImGui::Image(texId, ImVec2(100, 56));
                    } else {
                        ImGui::Dummy(ImVec2(100, 56));
                    }
                } else {
                    // No valid mesh - show placeholder
                    ImGui::Dummy(ImVec2(100, 56));
                    ImVec2 min = ImGui::GetItemRectMin();
                    ImVec2 max = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(30, 50, 70, 255), 4.0f);
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(min.x + 20, min.y + 20), IM_COL32(100, 180, 255, 255), "no mesh");
                }
            }
        } else if (kind == vivid::OutputKind::Value || kind == vivid::OutputKind::ValueArray) {
            // Value operator - show numeric display
            ImGui::Dummy(ImVec2(100, 40));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(50, 40, 30, 255), 4.0f);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(min.x + 25, min.y + 12), IM_COL32(200, 180, 100, 255),
                kind == vivid::OutputKind::Value ? "Value" : "Values");
        } else if (kind == vivid::OutputKind::Camera) {
            // Camera operator - draw camera icon
            ImGui::Dummy(ImVec2(100, 50));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(30, 60, 50, 255), 4.0f);

            // Draw camera body (rectangle)
            float cx = (min.x + max.x) * 0.5f;
            float cy = (min.y + max.y) * 0.5f;
            ImU32 iconColor = IM_COL32(100, 200, 160, 255);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(cx - 20, cy - 10), ImVec2(cx + 10, cy + 10), iconColor, 3.0f);
            // Draw lens (triangle)
            ImGui::GetWindowDrawList()->AddTriangleFilled(
                ImVec2(cx + 10, cy - 8), ImVec2(cx + 25, cy), ImVec2(cx + 10, cy + 8), iconColor);
            // Draw viewfinder
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(cx - 15, cy - 18), ImVec2(cx, cy - 10), iconColor, 2.0f);
        } else if (kind == vivid::OutputKind::Light) {
            // Light operator - draw light bulb icon
            ImGui::Dummy(ImVec2(100, 50));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(60, 50, 25, 255), 4.0f);

            // Draw bulb (circle)
            float cx = (min.x + max.x) * 0.5f;
            float cy = (min.y + max.y) * 0.5f - 3;
            ImU32 iconColor = IM_COL32(255, 220, 100, 255);
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cx, cy), 12, iconColor);
            // Draw base
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(cx - 6, cy + 10), ImVec2(cx + 6, cy + 18), IM_COL32(180, 180, 180, 255), 2.0f);
            // Draw rays
            ImU32 rayColor = IM_COL32(255, 240, 150, 180);
            for (int i = 0; i < 8; i++) {
                float angle = i * 3.14159f / 4;
                float r1 = 15, r2 = 22;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(cx + r1 * cos(angle), cy + r1 * sin(angle)),
                    ImVec2(cx + r2 * cos(angle), cy + r2 * sin(angle)),
                    rayColor, 2.0f);
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

    ImNodes::EndNodeEditor();

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

    // Save sidecar file if dirty
    if (m_sidecarDirty) {
        saveSidecar();
        m_sidecarDirty = false;
    }
}

std::string ChainVisualizer::makeParamKey(const std::string& opName, const std::string& paramName) {
    return opName + "." + paramName;
}

void ChainVisualizer::loadSidecar(const std::string& chainPath) {
    // Derive sidecar path from chain path: chain.cpp -> chain.vivid.json
    size_t lastDot = chainPath.rfind('.');
    if (lastDot != std::string::npos) {
        m_sidecarPath = chainPath.substr(0, lastDot) + ".vivid.json";
    } else {
        m_sidecarPath = chainPath + ".vivid.json";
    }

    // Try to read the file
    std::ifstream file(m_sidecarPath);
    if (!file.is_open()) {
        return;  // No sidecar file exists yet
    }

    // Simple JSON parsing (just key:value pairs for floats)
    std::string line;
    std::string currentOp;
    while (std::getline(file, line)) {
        // Skip empty lines and braces
        if (line.empty() || line.find('{') != std::string::npos ||
            line.find('}') != std::string::npos) {
            continue;
        }

        // Look for "key": [v0, v1, v2, v3]
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;

        // Extract key (remove quotes and whitespace)
        std::string key = line.substr(0, colonPos);
        size_t keyStart = key.find('"');
        size_t keyEnd = key.rfind('"');
        if (keyStart == std::string::npos || keyEnd <= keyStart) continue;
        key = key.substr(keyStart + 1, keyEnd - keyStart - 1);

        // Extract values
        std::string valueStr = line.substr(colonPos + 1);
        std::array<float, 4> values = {0, 0, 0, 0};

        // Find array brackets
        size_t bracketStart = valueStr.find('[');
        size_t bracketEnd = valueStr.find(']');
        if (bracketStart != std::string::npos && bracketEnd != std::string::npos) {
            std::string arrayStr = valueStr.substr(bracketStart + 1, bracketEnd - bracketStart - 1);
            int idx = 0;
            size_t pos = 0;
            while (pos < arrayStr.length() && idx < 4) {
                size_t commaPos = arrayStr.find(',', pos);
                if (commaPos == std::string::npos) commaPos = arrayStr.length();
                std::string numStr = arrayStr.substr(pos, commaPos - pos);
                try {
                    values[idx++] = std::stof(numStr);
                } catch (...) {}
                pos = commaPos + 1;
            }
        }

        m_paramOverrides[key] = values;
    }
}

void ChainVisualizer::saveSidecar() {
    if (m_sidecarPath.empty() || m_paramOverrides.empty()) {
        return;
    }

    std::ofstream file(m_sidecarPath);
    if (!file.is_open()) {
        return;
    }

    file << "{\n";
    bool first = true;
    for (const auto& [key, values] : m_paramOverrides) {
        if (!first) file << ",\n";
        first = false;
        file << "  \"" << key << "\": [" << values[0] << ", " << values[1]
             << ", " << values[2] << ", " << values[3] << "]";
    }
    file << "\n}\n";
}

void ChainVisualizer::applyOverrides(const std::vector<vivid::OperatorInfo>& operators) {
    for (const auto& info : operators) {
        if (!info.op) continue;

        auto params = info.op->params();
        for (const auto& param : params) {
            std::string key = makeParamKey(info.name, param.name);
            auto it = m_paramOverrides.find(key);
            if (it != m_paramOverrides.end()) {
                info.op->setParam(param.name, it->second.data());
            }
        }
    }
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

    if (m_exporter.start(outputPath, width, height, fps, codec)) {
        printf("[ChainVisualizer] Recording started: %s\n", outputPath.c_str());
    } else {
        printf("[ChainVisualizer] Failed to start recording: %s\n", m_exporter.error().c_str());
    }
}

void ChainVisualizer::stopRecording() {
    m_exporter.stop();
}

} // namespace vivid::imgui
