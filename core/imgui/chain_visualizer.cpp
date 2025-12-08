// Vivid Chain Visualizer Implementation
// Shows registered operators as nodes with connections

#include "chain_visualizer.h"
#include <imgui.h>
#include <imnodes.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
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

    // Dim the grid - it's too bright/distracting
    ImNodes::GetStyle().Colors[ImNodesCol_GridBackground] = IM_COL32(20, 20, 20, 255);
    ImNodes::GetStyle().Colors[ImNodesCol_GridLine] = IM_COL32(40, 40, 40, 255);
    ImNodes::GetStyle().Colors[ImNodesCol_GridLinePrimary] = IM_COL32(50, 50, 50, 255);

    m_initialized = true;
}

void ChainVisualizer::shutdown() {
    if (!m_initialized) return;

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

void ChainVisualizer::render(const FrameInput& input, vivid::Context& ctx) {
    if (!m_initialized) {
        init();
    }

    const auto& operators = ctx.registeredOperators();

    // Performance overlay
    float fps = input.dt > 0 ? 1.0f / input.dt : 0.0f;
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 100), ImGuiCond_FirstUseEver);
    ImGui::Begin("Performance", nullptr, ImGuiWindowFlags_NoResize);
    ImGui::Text("DT: %.3fms", input.dt * 1000.0f);
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Size: %dx%d", input.width, input.height);
    ImGui::Text("Operators: %zu", operators.size());
    ImGui::End();

    // Controls info
    ImGui::SetNextWindowPos(ImVec2(10, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 100), ImGuiCond_FirstUseEver);
    ImGui::Begin("Controls");
    ImGui::Text("Tab: Toggle UI");
    ImGui::Text("F: Fullscreen");
    ImGui::Text("Ctrl+Drag: Pan graph");
    ImGui::End();

    // Node editor
    ImGui::SetNextWindowPos(ImVec2(220, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Chain Visualizer");

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

        // Title bar - show registered name
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(info.name.c_str());
        ImNodes::EndNodeTitleBar();

        // Show operator type if different from registered name
        std::string typeName = info.op->name();
        if (typeName != info.name) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "%s", typeName.c_str());
        }

        // Show parameters if operator declares them
        auto params = info.op->params();
        if (!params.empty()) {
            // Draw a separator line within the node (ImGui::Separator extends too far)
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(pos.x, pos.y + 2),
                ImVec2(pos.x + 100, pos.y + 2),  // Match thumbnail width
                IM_COL32(80, 80, 90, 255), 1.0f);
            ImGui::Dummy(ImVec2(0, 6));  // Space for the line

            for (const auto& p : params) {
                switch (p.type) {
                    case vivid::ParamType::Float:
                        ImGui::Text("%s: %.2f", p.name.c_str(), p.defaultVal[0]);
                        break;
                    case vivid::ParamType::Int:
                        ImGui::Text("%s: %d", p.name.c_str(), static_cast<int>(p.defaultVal[0]));
                        break;
                    case vivid::ParamType::Bool:
                        ImGui::Text("%s: %s", p.name.c_str(), p.defaultVal[0] > 0.5f ? "true" : "false");
                        break;
                    case vivid::ParamType::Vec2:
                        ImGui::Text("%s: (%.2f, %.2f)", p.name.c_str(), p.defaultVal[0], p.defaultVal[1]);
                        break;
                    case vivid::ParamType::Vec3:
                    case vivid::ParamType::Color:
                        ImGui::Text("%s: (%.2f, %.2f, %.2f)", p.name.c_str(),
                            p.defaultVal[0], p.defaultVal[1], p.defaultVal[2]);
                        break;
                    case vivid::ParamType::Vec4:
                        ImGui::Text("%s: (%.2f, %.2f, %.2f, %.2f)", p.name.c_str(),
                            p.defaultVal[0], p.defaultVal[1], p.defaultVal[2], p.defaultVal[3]);
                        break;
                    case vivid::ParamType::String:
                        // String params encode value in the name (e.g., "mode: Multiply")
                        ImGui::Text("%s", p.name.c_str());
                        break;
                    default:
                        ImGui::Text("%s", p.name.c_str());
                        break;
                }
            }
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

    ImGui::End();
}

} // namespace vivid::imgui
