// Vivid Chain Visualizer Implementation
// Shows registered operators as nodes with connections
//
// Addon-agnostic: operators provide their own visualization via drawVisualization().
// No direct dependencies on audio, render3d, or other addons.

#include <vivid/chain_visualizer.h>
#include <vivid/viz_draw_list.h>
#include <vivid/operator_viz.h>
#include <vivid/audio_operator.h>
#include <vivid/audio_graph.h>
#include <vivid/asset_loader.h>
#include <vivid/frame_input.h>
#include <vivid/effects/texture_operator.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <climits>
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

namespace vivid {

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

ChainVisualizer::~ChainVisualizer() {
    shutdown();
}

void ChainVisualizer::init() {
    if (m_initialized) return;
    // NodeGraph initialization happens in initNodeGraph() - called lazily
    m_initialized = true;
}

void ChainVisualizer::shutdown() {
    if (!m_initialized) return;

    // Exit solo mode if active
    if (m_inSoloMode) {
        exitSoloMode();
    }

    m_initialized = false;
}

void ChainVisualizer::selectNodeFromEditor(const std::string& operatorName) {
    // Store the selection to be applied in next render() call
    // (ImNodes calls must happen within the node editor context)
    m_pendingEditorSelection = operatorName;
}


void ChainVisualizer::enterSoloMode(vivid::Operator* op, const std::string& name) {
    m_soloOperator = op;
    m_soloOperatorName = name;
    m_inSoloMode = true;
}

void ChainVisualizer::exitSoloMode() {
    m_soloOperator = nullptr;
    m_soloOperatorName.clear();
    m_inSoloMode = false;
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

// -------------------------------------------------------------------------
// New NodeGraph System (testing)
// -------------------------------------------------------------------------

void ChainVisualizer::initNodeGraph(vivid::Context& ctx, WGPUTextureFormat surfaceFormat) {
    if (m_nodeGraphInitialized) return;

    // Initialize overlay canvas with correct surface format
    if (!m_overlay.init(ctx, surfaceFormat)) {
        std::cerr << "[ChainVisualizer] Failed to initialize OverlayCanvas\n";
        return;
    }

    // Load fonts for node graph:
    // Font index 0: Inter Regular (body text, labels)
    // Font index 1: Inter Medium (node titles only)
    // Font index 2: Roboto Mono (numeric displays - FPS, timings, etc.)
    auto exeDir = AssetLoader::instance().executableDir();
    auto projectRoot = exeDir.parent_path().parent_path();  // build/bin -> build -> project

    // Paths to font files
    std::string regularPath = (projectRoot / "assets/fonts/Inter/static/Inter_18pt-Regular.ttf").string();
    std::string mediumPath = (projectRoot / "assets/fonts/Inter/static/Inter_18pt-Medium.ttf").string();
    std::string monoPath = (projectRoot / "assets/fonts/Roboto_Mono/static/RobotoMono-Regular.ttf").string();

    // Load Inter Regular as primary font (index 0) - for tooltips/labels
    if (m_overlay.loadFont(ctx, regularPath, 16.0f)) {
        std::cerr << "[ChainVisualizer] Loaded Inter Regular (16px)\n";
    } else {
        std::cerr << "[ChainVisualizer] Warning: Could not load Inter Regular font\n";
    }

    // Load Inter Medium for node titles (index 1)
    if (m_overlay.loadFontSize(ctx, mediumPath, 18.0f, 1)) {
        std::cerr << "[ChainVisualizer] Loaded Inter Medium (18px) for titles\n";
    } else {
        std::cerr << "[ChainVisualizer] Warning: Could not load Inter Medium font\n";
    }

    // Load Roboto Mono for numeric displays (index 2) - status bar
    if (m_overlay.loadFontSize(ctx, monoPath, 14.0f, 2)) {
        std::cerr << "[ChainVisualizer] Loaded Roboto Mono (14px) for metrics\n";
    } else {
        std::cerr << "[ChainVisualizer] Warning: Could not load Roboto Mono font\n";
    }

    m_nodeGraphInitialized = true;
    std::cerr << "[ChainVisualizer] NodeGraph initialized\n";
}

void ChainVisualizer::renderNodeGraph(WGPURenderPassEncoder pass, const FrameInput& input, vivid::Context& ctx) {
    if (!m_nodeGraphInitialized) {
        initNodeGraph(ctx, input.surfaceFormat);
        if (!m_nodeGraphInitialized) return;
    }

    const auto& operators = ctx.registeredOperators();
    if (operators.empty()) return;

    // Build input for node graph
    // Scale mouse from window coords to framebuffer coords (for HiDPI/Retina)
    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;
    glm::vec2 scaledMousePos = input.mousePos * scale;

    vivid::NodeGraphInput graphInput;
    graphInput.mousePos = scaledMousePos;
    // Calculate mouse delta from previous frame
    static glm::vec2 lastMousePos = scaledMousePos;
    graphInput.mouseDelta = scaledMousePos - lastMousePos;
    lastMousePos = scaledMousePos;
    graphInput.scroll = input.scroll;
    graphInput.mouseDown[0] = input.mouseDown[0];
    graphInput.mouseDown[1] = input.mouseDown[1];
    graphInput.mouseDown[2] = input.mouseDown[2];
    // Track clicks
    static bool lastMouseDown[3] = {false, false, false};
    for (int i = 0; i < 3; i++) {
        graphInput.mouseClicked[i] = input.mouseDown[i] && !lastMouseDown[i];
        graphInput.mouseReleased[i] = !input.mouseDown[i] && lastMouseDown[i];
        lastMouseDown[i] = input.mouseDown[i];
    }
    graphInput.keyCtrl = input.keyCtrl;
    graphInput.keyShift = input.keyShift;
    graphInput.keyAlt = input.keyAlt;
    // Key presses for shortcuts
    graphInput.keyF = input.isKeyPressed(vivid::Key::F);
    graphInput.key1 = input.isKeyPressed(vivid::Key::Num1);
    graphInput.keyUp = input.isKeyPressed(vivid::Key::Up);
    graphInput.keyDown = input.isKeyPressed(vivid::Key::Down);
    graphInput.keyLeft = input.isKeyPressed(vivid::Key::Left);
    graphInput.keyRight = input.isKeyPressed(vivid::Key::Right);
    graphInput.keyEnter = input.isKeyPressed(vivid::Key::Enter);
    graphInput.keyB = input.isKeyPressed(vivid::Key::B);
    graphInput.keyEscape = input.isKeyPressed(vivid::Key::Escape);

    // Begin overlay rendering
    m_overlay.begin(input.width, input.height);

    // Begin node graph editor
    m_nodeGraph.beginEditor(m_overlay, static_cast<float>(input.width), static_cast<float>(input.height), graphInput);

    // Add nodes for each operator
    for (size_t i = 0; i < operators.size(); ++i) {
        const vivid::OperatorInfo& info = operators[i];
        if (!info.op) continue;

        int nodeId = static_cast<int>(i);

        m_nodeGraph.beginNode(nodeId);
        m_nodeGraph.setNodeTitle(info.name);

        // Set content callback to render operator preview/thumbnail
        vivid::Operator* op = info.op;  // Capture for lambda
        m_nodeGraph.setNodeContent([op](OverlayCanvas& canvas, float x, float y, float w, float h) {
            if (!op) return;

            vivid::OutputKind kind = op->outputKind();

            // First try operator's custom visualization via VizDrawList
            VizDrawList dl(canvas);
            if (op->drawVisualization(&dl, x, y, x + w, y + h)) {
                return; // Operator drew its own visualization
            }

            // Fallback visualization based on output type
            if (kind == vivid::OutputKind::Texture) {
                // Render texture preview with aspect ratio preservation
                WGPUTextureView view = op->outputView();
                if (view) {
                    // Get texture dimensions from operator if available
                    float srcAspect = 16.0f / 9.0f;  // Default to 16:9
                    if (auto* texOp = dynamic_cast<vivid::effects::TextureOperator*>(op)) {
                        int texW = texOp->outputWidth();
                        int texH = texOp->outputHeight();
                        if (texW > 0 && texH > 0) {
                            srcAspect = static_cast<float>(texW) / static_cast<float>(texH);
                        }
                    }

                    // Preserve aspect ratio - fit image within area
                    float areaAspect = w / h;
                    float drawW, drawH, drawX, drawY;

                    if (srcAspect > areaAspect) {
                        // Image is wider - fit to width
                        drawW = w;
                        drawH = w / srcAspect;
                        drawX = x;
                        drawY = y + (h - drawH) * 0.5f;
                    } else {
                        // Image is taller - fit to height
                        drawH = h;
                        drawW = h * srcAspect;
                        drawX = x + (w - drawW) * 0.5f;
                        drawY = y;
                    }

                    canvas.texturedRect(drawX, drawY, drawW, drawH, view);
                } else {
                    // No texture yet - draw placeholder
                    canvas.fillRect(x, y, w, h, {0.15f, 0.15f, 0.2f, 1.0f});
                }
            } else if (kind == vivid::OutputKind::Geometry) {
                // Geometry - draw 3D cube icon
                canvas.fillRect(x, y, w, h, {0.12f, 0.2f, 0.28f, 1.0f});
                float cx = x + w * 0.5f;
                float cy = y + h * 0.5f;
                float sz = std::min(w, h) * 0.3f;
                // Simple wireframe cube representation
                glm::vec4 lineColor = {0.4f, 0.7f, 1.0f, 0.8f};
                canvas.strokeRect(cx - sz, cy - sz * 0.6f, sz * 1.6f, sz * 1.2f, 1.5f, lineColor);
            } else if (kind == vivid::OutputKind::Audio) {
                // Audio - draw waveform icon
                canvas.fillRect(x, y, w, h, {0.2f, 0.12f, 0.25f, 1.0f});
                float centerY = y + h * 0.5f;
                glm::vec4 waveColor = {0.7f, 0.5f, 0.9f, 0.9f};
                // Draw simple wave
                float prevX = x + 4;
                float prevY = centerY;
                for (int i = 1; i <= 8; i++) {
                    float px = x + 4 + (w - 8) * i / 8.0f;
                    float amplitude = (i % 2 == 0) ? 0.3f : -0.25f;
                    float py = centerY + amplitude * h * 0.6f;
                    canvas.line(prevX, prevY, px, py, 2.0f, waveColor);
                    prevX = px;
                    prevY = py;
                }
            } else {
                // Other types - generic placeholder
                canvas.fillRect(x, y, w, h, {0.15f, 0.15f, 0.18f, 1.0f});
            }
        });

        // Add input pins for each connected input
        size_t numInputs = info.op->inputCount();
        for (size_t j = 0; j < numInputs; ++j) {
            if (info.op->getInput(static_cast<int>(j))) {
                int pinId = nodeId * 100 + static_cast<int>(j) + 1;
                m_nodeGraph.beginInputAttribute(pinId);
                // Use input name if available, otherwise "in{j}"
                std::string label = info.op->getInputName(static_cast<int>(j));
                if (label.empty()) {
                    label = "in" + std::to_string(j);
                }
                m_nodeGraph.pinLabel(label);
                m_nodeGraph.endInputAttribute();
            }
        }

        // Add output pin
        int outputPinId = nodeId * 100;
        m_nodeGraph.beginOutputAttribute(outputPinId);
        m_nodeGraph.pinLabel("out");
        m_nodeGraph.endOutputAttribute();

        m_nodeGraph.endNode();
    }

    // Add Screen output node (represents the display)
    vivid::Operator* outputOp = ctx.hasChain() ? ctx.chain().getOutput() : nullptr;
    int outputNodeId = -1;
    if (outputOp) {
        for (size_t i = 0; i < operators.size(); ++i) {
            if (operators[i].op == outputOp) {
                outputNodeId = static_cast<int>(i);
                break;
            }
        }

        if (outputNodeId >= 0) {
            m_nodeGraph.beginNode(SCREEN_NODE_ID);
            m_nodeGraph.setNodeTitle("Screen");
            m_nodeGraph.beginInputAttribute(SCREEN_NODE_ID * 100 + 1);
            m_nodeGraph.pinLabel("display");
            m_nodeGraph.endInputAttribute();
            m_nodeGraph.endNode();
        }
    }

    // Add Speakers output node (represents audio output)
    vivid::Operator* audioOutputOp = ctx.hasChain() ? ctx.chain().getAudioOutput() : nullptr;
    int audioOutputNodeId = -1;
    if (audioOutputOp) {
        for (size_t i = 0; i < operators.size(); ++i) {
            if (operators[i].op == audioOutputOp) {
                audioOutputNodeId = static_cast<int>(i);
                break;
            }
        }

        if (audioOutputNodeId >= 0) {
            m_nodeGraph.beginNode(SPEAKERS_NODE_ID);
            m_nodeGraph.setNodeTitle("Speakers");
            m_nodeGraph.beginInputAttribute(SPEAKERS_NODE_ID * 100 + 1);
            m_nodeGraph.pinLabel("audio");
            m_nodeGraph.endInputAttribute();
            m_nodeGraph.endNode();
        }
    }


    // Add links based on operator connections
    // (must be done before autoLayout for crossing reduction to work)
    int linkId = 0;
    for (size_t i = 0; i < operators.size(); ++i) {
        const vivid::OperatorInfo& info = operators[i];
        if (!info.op) continue;

        int nodeId = static_cast<int>(i);
        size_t numInputs = info.op->inputCount();

        for (size_t j = 0; j < numInputs; ++j) {
            vivid::Operator* inputOp = info.op->getInput(static_cast<int>(j));
            if (!inputOp) continue;

            // Find the node ID for the input operator
            for (size_t k = 0; k < operators.size(); ++k) {
                if (operators[k].op == inputOp) {
                    int srcNodeId = static_cast<int>(k);
                    int srcOutputPinId = srcNodeId * 100;
                    int dstInputPinId = nodeId * 100 + static_cast<int>(j) + 1;
                    m_nodeGraph.link(linkId++, srcOutputPinId, dstInputPinId);
                    break;
                }
            }
        }
    }

    // Link from output operator to Screen node
    if (outputNodeId >= 0) {
        m_nodeGraph.link(linkId++, outputNodeId * 100, SCREEN_NODE_ID * 100 + 1);
    }

    // Link from audio output operator to Speakers node
    if (audioOutputNodeId >= 0) {
        m_nodeGraph.link(linkId++, audioOutputNodeId * 100, SPEAKERS_NODE_ID * 100 + 1);
    }

    // Do hierarchical layout using Sugiyama algorithm (with crossing reduction)
    static bool autoLayoutDone = false;
    if (!autoLayoutDone && !operators.empty()) {
        m_nodeGraph.autoLayout();
        m_nodeGraph.zoomToFit();
        autoLayoutDone = true;
    }

    // End node graph editor
    m_nodeGraph.endEditor();

    // Render status bar (in screen space, not node graph space)
    m_overlay.resetTransform();
    renderStatusBar(input, ctx);

    // Handle status bar button clicks
    if (graphInput.mouseClicked[0]) {
        glm::vec2 mousePos = graphInput.mousePos;

        // Check codec dropdown menu items first (when open)
        if (m_codecDropdownOpen) {
            if (isMouseInRect(m_codecH264, mousePos)) {
                startRecording(ExportCodec::H264, ctx);
                m_codecDropdownOpen = false;
            } else if (isMouseInRect(m_codecH265, mousePos)) {
                startRecording(ExportCodec::H265, ctx);
                m_codecDropdownOpen = false;
            } else if (isMouseInRect(m_codecProRes, mousePos)) {
                startRecording(ExportCodec::Animation, ctx);
                m_codecDropdownOpen = false;
            } else if (isMouseInRect(m_recordButton, mousePos)) {
                // Clicked record button while open - keep open
            } else {
                // Clicked elsewhere - close dropdown
                m_codecDropdownOpen = false;
            }
        } else {
            // Dropdown closed - handle normal button clicks
            if (isMouseInRect(m_recordButton, mousePos)) {
                // Toggle dropdown
                m_codecDropdownOpen = true;
            } else if (isMouseInRect(m_stopButton, mousePos)) {
                stopRecording(ctx);
            } else if (isMouseInRect(m_snapshotButton, mousePos)) {
                requestSnapshot();
            }
        }
    }

    // Render tooltip for hovered node
    int hoveredNodeId = -1;
    if (m_nodeGraph.isNodeHovered(&hoveredNodeId) && hoveredNodeId >= 0 &&
        hoveredNodeId != SCREEN_NODE_ID && hoveredNodeId != SPEAKERS_NODE_ID) {
        // Find the operator for this node
        if (static_cast<size_t>(hoveredNodeId) < operators.size()) {
            const vivid::OperatorInfo& info = operators[hoveredNodeId];
            if (info.op) {
                renderTooltip(input, info);
            }
        }
    }

    // Render debug values panel (bottom-left corner)
    renderDebugPanelOverlay(input, ctx);

    // Handle keyboard shortcuts (using new key input system)
    using vivid::Key;

    // S key - solo selected node
    if (input.isKeyPressed(Key::S)) {
        int selectedNodeId = m_nodeGraph.getSelectedNode();
        if (selectedNodeId >= 0 && selectedNodeId != SCREEN_NODE_ID && selectedNodeId != SPEAKERS_NODE_ID) {
            if (static_cast<size_t>(selectedNodeId) < operators.size()) {
                const vivid::OperatorInfo& info = operators[selectedNodeId];
                if (info.op) {
                    enterSoloMode(info.op, info.name);
                }
            }
        }
    }

    // Escape key - exit solo mode
    if (input.isKeyPressed(Key::Escape) && m_inSoloMode) {
        exitSoloMode();
    }

    // B key - toggle bypass on selected node
    if (input.isKeyPressed(Key::B)) {
        int selectedNodeId = m_nodeGraph.getSelectedNode();
        if (selectedNodeId >= 0 && selectedNodeId != SCREEN_NODE_ID && selectedNodeId != SPEAKERS_NODE_ID) {
            if (static_cast<size_t>(selectedNodeId) < operators.size()) {
                const vivid::OperatorInfo& info = operators[selectedNodeId];
                if (info.op) {
                    info.op->setBypassed(!info.op->isBypassed());
                }
            }
        }
    }

    // Render solo mode overlay (if active)
    if (m_inSoloMode && m_soloOperator) {
        // Set the output texture to the solo operator's output
        vivid::OutputKind kind = m_soloOperator->outputKind();
        if (kind == vivid::OutputKind::Texture) {
            WGPUTextureView view = m_soloOperator->outputView();
            if (view) {
                ctx.setOutputTexture(view);
            }
        }

        // Draw solo mode indicator (top-left corner, using topmost layer)
        float lineH = m_overlay.fontLineHeight(0);
        float ascent = m_overlay.fontAscent(0);
        if (lineH <= 0) lineH = 22.0f;
        if (ascent <= 0) ascent = 16.0f;

        const float padding = 10.0f;
        std::string soloText = "SOLO: " + m_soloOperatorName;
        std::string escText = "(press ESC to exit)";
        float soloWidth = m_overlay.measureText(soloText, 0);
        float escWidth = m_overlay.measureText(escText, 0);
        float boxWidth = std::max(soloWidth, escWidth) + padding * 2;
        float boxHeight = lineH * 2 + padding * 2;

        glm::vec4 bgColor = {0.15f, 0.12f, 0.05f, 0.9f};
        glm::vec4 borderColor = {0.8f, 0.6f, 0.2f, 1.0f};
        glm::vec4 soloColor = {1.0f, 0.9f, 0.4f, 1.0f};
        glm::vec4 dimColor = {0.6f, 0.6f, 0.7f, 1.0f};

        m_overlay.fillRoundedRectTopmost(padding, padding, boxWidth, boxHeight, 4.0f, bgColor);
        m_overlay.strokeRoundedRectTopmost(padding, padding, boxWidth, boxHeight, 4.0f, 1.0f, borderColor);
        m_overlay.textTopmost(soloText, padding * 2, padding + ascent, soloColor);
        m_overlay.textTopmost(escText, padding * 2, padding + lineH + ascent, dimColor);
    }

    // Render the overlay
    m_overlay.render(pass);
}

void ChainVisualizer::renderStatusBar(const FrameInput& input, vivid::Context& ctx) {
    // Use mono font metrics for bar height calculation
    const int monoFont = 2;
    float lineH = m_overlay.fontLineHeight(monoFont);
    float ascent = m_overlay.fontAscent(monoFont);
    if (lineH <= 0) lineH = 20.0f;  // Fallback
    if (ascent <= 0) ascent = 14.0f;

    const float padding = 6.0f;
    const float barHeight = lineH + padding * 2;
    float x = padding;
    // Position text so it's vertically centered (baseline = top padding + ascent)
    float y = padding + ascent;

    // Smoothed values for FPS and frame time (exponential moving average)
    static float smoothedFps = 60.0f;
    static float smoothedMs = 16.67f;
    const float smoothing = 0.05f;  // Lower = smoother (0.05 = ~20 frame average)

    float instantFps = input.dt > 0 ? 1.0f / input.dt : smoothedFps;
    float instantMs = input.dt * 1000.0f;
    smoothedFps = smoothedFps + smoothing * (instantFps - smoothedFps);
    smoothedMs = smoothedMs + smoothing * (instantMs - smoothedMs);

    // Semi-transparent background
    m_overlay.fillRect(0, 0, static_cast<float>(input.width), barHeight,
                       {0.1f, 0.1f, 0.12f, 0.85f});

    // Colors
    glm::vec4 textColor = {0.9f, 0.9f, 0.9f, 1.0f};
    glm::vec4 dimColor = {0.5f, 0.5f, 0.55f, 1.0f};
    glm::vec4 greenColor = {0.4f, 0.9f, 0.4f, 1.0f};
    glm::vec4 yellowColor = {0.9f, 0.9f, 0.4f, 1.0f};
    glm::vec4 redColor = {0.9f, 0.4f, 0.4f, 1.0f};

    char buf[64];
    // Separator line inset from top/bottom
    const float sepInset = padding;

    // FPS (fixed width: 5 chars for number + " FPS")
    snprintf(buf, sizeof(buf), "%5.1f FPS", smoothedFps);
    m_overlay.text(buf, x, y, textColor, monoFont);
    x += m_overlay.measureText(buf, monoFont) + padding * 2;

    // Separator
    m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Frame time (fixed width: 6 chars for number + "ms")
    snprintf(buf, sizeof(buf), "%6.2fms", smoothedMs);
    m_overlay.text(buf, x, y, textColor, monoFont);
    x += m_overlay.measureText(buf, monoFont) + padding * 2;

    // Separator
    m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Resolution (fixed width for common resolutions)
    snprintf(buf, sizeof(buf), "%4dx%-4d", input.width, input.height);
    m_overlay.text(buf, x, y, textColor, monoFont);
    x += m_overlay.measureText(buf, monoFont) + padding * 2;

    // Separator
    m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Operator count
    const auto& operators = ctx.registeredOperators();
    snprintf(buf, sizeof(buf), "%2zu ops", operators.size());
    m_overlay.text(buf, x, y, textColor, monoFont);
    x += m_overlay.measureText(buf, monoFont) + padding * 2;

    // Separator
    m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Memory usage (color-coded)
    size_t memBytes = getProcessMemoryUsage();
    std::string memStr = formatMemory(memBytes);
    glm::vec4 memColor;
    if (memBytes < 500 * 1024 * 1024) {
        memColor = greenColor;
    } else if (memBytes < 2ULL * 1024 * 1024 * 1024) {
        memColor = yellowColor;
    } else {
        memColor = redColor;
    }
    m_overlay.text("MEM:", x, y, dimColor);
    x += m_overlay.measureText("MEM:") + 4;
    m_overlay.text(memStr, x, y, memColor, monoFont);
    x += m_overlay.measureText(memStr, monoFont) + padding * 2;

    // Audio stats (if audio active)
    AudioGraph* audioGraph = ctx.chain().audioGraph();
    if (audioGraph && !audioGraph->empty()) {
        // Separator
        m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
        x += padding * 2;

        // DSP Load
        float dspLoad = audioGraph->dspLoad();
        glm::vec4 dspColor;
        if (dspLoad < 0.5f) {
            dspColor = greenColor;
        } else if (dspLoad < 0.8f) {
            dspColor = yellowColor;
        } else {
            dspColor = redColor;
        }
        m_overlay.text("DSP:", x, y, dimColor);
        x += m_overlay.measureText("DSP:") + 4;
        snprintf(buf, sizeof(buf), "%3.0f%%", dspLoad * 100.0f);
        m_overlay.text(buf, x, y, dspColor, monoFont);
        x += m_overlay.measureText(buf, monoFont) + padding * 2;

        // Dropped events (if any)
        uint64_t dropped = audioGraph->droppedEventCount();
        if (dropped > 0) {
            snprintf(buf, sizeof(buf), "%llu dropped", dropped);
            m_overlay.text(buf, x, y, redColor, monoFont);
            x += m_overlay.measureText(buf, monoFont) + padding * 2;
        }
    }

    // Recording controls (right side)
    // Reset button hit regions
    m_recordButton.valid = false;
    m_stopButton.valid = false;
    m_snapshotButton.valid = false;

    glm::vec4 buttonBg = {0.25f, 0.25f, 0.3f, 1.0f};
    glm::vec4 buttonHover = {0.35f, 0.35f, 0.4f, 1.0f};
    glm::vec4 buttonBorder = {0.4f, 0.4f, 0.45f, 1.0f};
    const float buttonPadX = 8.0f;
    const float buttonPadY = 4.0f;
    const float buttonSpacing = 6.0f;

    if (m_exporter.isRecording()) {
        // Recording active: show REC indicator + Stop button
        snprintf(buf, sizeof(buf), "REC %d frames (%.1fs)",
                 m_exporter.frameCount(), m_exporter.duration());
        float recTextWidth = m_overlay.measureText(buf, monoFont);

        // Stop button
        const char* stopText = "Stop";
        float stopTextWidth = m_overlay.measureText(stopText, monoFont);
        float stopBtnW = stopTextWidth + buttonPadX * 2;
        float stopBtnH = lineH + buttonPadY * 2;
        float stopBtnX = input.width - stopBtnW - padding;
        float stopBtnY = (barHeight - stopBtnH) * 0.5f;

        m_stopButton = {stopBtnX, stopBtnY, stopBtnW, stopBtnH, true};
        m_overlay.fillRoundedRect(stopBtnX, stopBtnY, stopBtnW, stopBtnH, 4, buttonBg);
        m_overlay.strokeRoundedRect(stopBtnX, stopBtnY, stopBtnW, stopBtnH, 4, 1, redColor);
        m_overlay.text(stopText, stopBtnX + buttonPadX, stopBtnY + buttonPadY + ascent, redColor, monoFont);

        // REC indicator (red dot + text)
        float recX = stopBtnX - recTextWidth - 24 - buttonSpacing;
        m_overlay.fillCircle(recX + 6, barHeight * 0.5f, 4, redColor);
        m_overlay.text(buf, recX + 16, y, redColor, monoFont);
    } else {
        // Not recording: show Snapshot + Record buttons
        float rightX = input.width - padding;

        // Snapshot button
        const char* snapText = "Snapshot";
        float snapTextWidth = m_overlay.measureText(snapText, monoFont);
        float snapBtnW = snapTextWidth + buttonPadX * 2;
        float snapBtnH = lineH + buttonPadY * 2;
        float snapBtnX = rightX - snapBtnW;
        float snapBtnY = (barHeight - snapBtnH) * 0.5f;

        m_snapshotButton = {snapBtnX, snapBtnY, snapBtnW, snapBtnH, true};
        m_overlay.fillRoundedRect(snapBtnX, snapBtnY, snapBtnW, snapBtnH, 4, buttonBg);
        m_overlay.strokeRoundedRect(snapBtnX, snapBtnY, snapBtnW, snapBtnH, 4, 1, buttonBorder);
        m_overlay.text(snapText, snapBtnX + buttonPadX, snapBtnY + buttonPadY + ascent, textColor, monoFont);

        // Record button (with dropdown indicator)
        const char* recText = "Record ▾";
        float recTextWidth = m_overlay.measureText(recText, monoFont);
        float recBtnW = recTextWidth + buttonPadX * 2 + 12;  // Extra for red dot
        float recBtnH = lineH + buttonPadY * 2;
        float recBtnX = snapBtnX - recBtnW - buttonSpacing;
        float recBtnY = (barHeight - recBtnH) * 0.5f;

        m_recordButton = {recBtnX, recBtnY, recBtnW, recBtnH, true};
        glm::vec4 recBtnBg = m_codecDropdownOpen ? buttonHover : buttonBg;
        m_overlay.fillRoundedRect(recBtnX, recBtnY, recBtnW, recBtnH, 4, recBtnBg);
        m_overlay.strokeRoundedRect(recBtnX, recBtnY, recBtnW, recBtnH, 4, 1, redColor);
        // Red dot in record button
        m_overlay.fillCircle(recBtnX + buttonPadX + 4, barHeight * 0.5f, 3, redColor);
        m_overlay.text(recText, recBtnX + buttonPadX + 12, recBtnY + buttonPadY + ascent, textColor, monoFont);

        // Codec dropdown menu (rendered in topmost layer so it appears over everything)
        m_codecH264.valid = false;
        m_codecH265.valid = false;
        m_codecProRes.valid = false;

        if (m_codecDropdownOpen) {
            const char* items[] = {"H.264 (recommended)", "H.265", "ProRes 4444"};
            float menuWidth = 0;
            for (const char* item : items) {
                menuWidth = std::max(menuWidth, m_overlay.measureText(item, monoFont));
            }
            menuWidth += buttonPadX * 2;

            float menuX = recBtnX;
            float menuY = barHeight + 2;
            float itemH = lineH + buttonPadY * 2;
            float menuH = itemH * 3;

            glm::vec4 menuBg = {0.18f, 0.18f, 0.2f, 0.98f};
            glm::vec4 itemHover = {0.3f, 0.3f, 0.35f, 1.0f};

            // Menu background
            m_overlay.fillRoundedRectTopmost(menuX, menuY, menuWidth, menuH, 4, menuBg);
            m_overlay.strokeRoundedRectTopmost(menuX, menuY, menuWidth, menuH, 4, 1, buttonBorder);

            // Menu items
            float itemY = menuY;
            m_codecH264 = {menuX, itemY, menuWidth, itemH, true};
            m_overlay.textTopmost(items[0], menuX + buttonPadX, itemY + buttonPadY + ascent, textColor, monoFont);

            itemY += itemH;
            m_codecH265 = {menuX, itemY, menuWidth, itemH, true};
            m_overlay.textTopmost(items[1], menuX + buttonPadX, itemY + buttonPadY + ascent, textColor, monoFont);

            itemY += itemH;
            m_codecProRes = {menuX, itemY, menuWidth, itemH, true};
            m_overlay.textTopmost(items[2], menuX + buttonPadX, itemY + buttonPadY + ascent, textColor, monoFont);
        }
    }
}

void ChainVisualizer::renderTooltip(const FrameInput& input, const vivid::OperatorInfo& info) {
    if (!info.op) return;

    // Use font 0 (Inter Regular) metrics for tooltip
    float lineH = m_overlay.fontLineHeight(0);
    float ascent = m_overlay.fontAscent(0);
    if (lineH <= 0) lineH = 22.0f;  // Fallback
    if (ascent <= 0) ascent = 16.0f;

    const float padding = 8.0f;
    const float lineHeight = lineH;
    float tooltipWidth = 200.0f;

    // Colors - fully opaque background for readability
    glm::vec4 bgColor = {0.12f, 0.12f, 0.14f, 1.0f};
    glm::vec4 borderColor = {0.4f, 0.4f, 0.45f, 1.0f};
    glm::vec4 titleColor = {0.5f, 0.8f, 1.0f, 1.0f};
    glm::vec4 textColor = {0.9f, 0.9f, 0.9f, 1.0f};
    glm::vec4 dimColor = {0.65f, 0.65f, 0.7f, 1.0f};
    glm::vec4 orangeColor = {1.0f, 0.6f, 0.3f, 1.0f};

    // Build tooltip content
    std::vector<std::pair<std::string, glm::vec4>> lines;

    // Operator type name
    lines.push_back({info.op->name(), titleColor});

    // Registered name if different
    if (info.name != info.op->name()) {
        lines.push_back({"(" + info.name + ")", dimColor});
    }

    // Output type
    vivid::OutputKind kind = info.op->outputKind();
    const char* kindStr = "Unknown";
    switch (kind) {
        case vivid::OutputKind::Texture: kindStr = "Output: Texture"; break;
        case vivid::OutputKind::Geometry: kindStr = "Output: Geometry"; break;
        case vivid::OutputKind::Audio: kindStr = "Output: Audio"; break;
        case vivid::OutputKind::AudioValue: kindStr = "Output: Audio Value"; break;
        case vivid::OutputKind::Value: kindStr = "Output: Value"; break;
        case vivid::OutputKind::ValueArray: kindStr = "Output: Value Array"; break;
        case vivid::OutputKind::Camera: kindStr = "Output: Camera"; break;
        case vivid::OutputKind::Light: kindStr = "Output: Light"; break;
    }
    lines.push_back({kindStr, textColor});

    // Resource info for textures
    if (kind == vivid::OutputKind::Texture) {
        WGPUTexture tex = info.op->outputTexture();
        if (tex) {
            uint32_t w = wgpuTextureGetWidth(tex);
            uint32_t h = wgpuTextureGetHeight(tex);
            size_t memBytes = w * h * 8;  // RGBA16Float = 8 bytes per pixel
            char buf[64];
            snprintf(buf, sizeof(buf), "Size: %ux%u", w, h);
            lines.push_back({buf, textColor});
            snprintf(buf, sizeof(buf), "Memory: ~%.1f MB", memBytes / (1024.0f * 1024.0f));
            lines.push_back({buf, textColor});
        } else {
            lines.push_back({"No texture", dimColor});
        }
    }

    // Bypass status
    if (info.op->isBypassed()) {
        lines.push_back({"BYPASSED", orangeColor});
    }

    // Calculate tooltip size
    float maxWidth = 0;
    for (const auto& line : lines) {
        float w = m_overlay.measureText(line.first);
        maxWidth = std::max(maxWidth, w);
    }
    tooltipWidth = maxWidth + padding * 2;
    float tooltipHeight = lines.size() * lineHeight + padding * 2;

    // Position tooltip near mouse (offset to not obscure cursor)
    float mouseX = input.mousePos.x * (input.contentScale > 0 ? input.contentScale : 1.0f);
    float mouseY = input.mousePos.y * (input.contentScale > 0 ? input.contentScale : 1.0f);
    float tooltipX = mouseX + 15;
    float tooltipY = mouseY + 15;

    // Keep on screen
    if (tooltipX + tooltipWidth > input.width) {
        tooltipX = mouseX - tooltipWidth - 10;
    }
    if (tooltipY + tooltipHeight > input.height) {
        tooltipY = mouseY - tooltipHeight - 10;
    }

    // Draw background (use topmost layer so tooltips appear above thumbnails)
    m_overlay.fillRoundedRectTopmost(tooltipX, tooltipY, tooltipWidth, tooltipHeight, 4.0f, bgColor);
    m_overlay.strokeRoundedRectTopmost(tooltipX, tooltipY, tooltipWidth, tooltipHeight, 4.0f, 1.0f, borderColor);

    // Draw text lines (position at baseline = top + ascent)
    float textY = tooltipY + padding + ascent;
    for (const auto& line : lines) {
        m_overlay.textTopmost(line.first, tooltipX + padding, textY, line.second);
        textY += lineHeight;
    }
}

void ChainVisualizer::renderDebugPanelOverlay(const FrameInput& input, vivid::Context& ctx) {
    const auto& debugValues = ctx.debugValues();
    if (debugValues.empty()) return;

    // Use font metrics for layout
    const int monoFont = 2;
    float lineH = m_overlay.fontLineHeight(monoFont);
    float ascent = m_overlay.fontAscent(monoFont);
    if (lineH <= 0) lineH = 20.0f;  // Fallback
    if (ascent <= 0) ascent = 14.0f;

    const float padding = 8.0f;
    const float lineHeight = lineH + 4;  // Add some spacing between rows
    const float nameWidth = 90.0f;
    const float sparklineWidth = 100.0f;
    const float sparklineHeight = lineH - 2;
    const float valueWidth = 65.0f;
    const float panelWidth = nameWidth + sparklineWidth + valueWidth + padding * 4;
    const float panelHeight = debugValues.size() * lineHeight + padding * 2;

    // Position in bottom-left corner
    float panelX = padding;
    float panelY = input.height - panelHeight - padding;

    // Colors
    glm::vec4 bgColor = {0.12f, 0.12f, 0.15f, 0.9f};
    glm::vec4 borderColor = {0.3f, 0.3f, 0.35f, 1.0f};
    glm::vec4 textColor = {0.85f, 0.85f, 0.85f, 1.0f};
    glm::vec4 dimColor = {0.5f, 0.5f, 0.55f, 1.0f};
    glm::vec4 graphColor = {0.4f, 0.7f, 0.9f, 1.0f};
    glm::vec4 graphBgColor = {0.08f, 0.08f, 0.1f, 1.0f};

    // Draw panel background
    m_overlay.fillRoundedRect(panelX, panelY, panelWidth, panelHeight, 4.0f, bgColor);
    m_overlay.strokeRoundedRect(panelX, panelY, panelWidth, panelHeight, 4.0f, 1.0f, borderColor);

    float y = panelY + padding;
    for (const auto& [name, dv] : debugValues) {
        float x = panelX + padding;

        glm::vec4 color = dv.updatedThisFrame ? textColor : dimColor;

        // Name (baseline positioned with ascent)
        m_overlay.text(name, x, y + ascent, color);
        x += nameWidth;

        // Sparkline background (vertically centered in line)
        float sparkY = y + (lineHeight - sparklineHeight) * 0.5f;
        m_overlay.fillRect(x, sparkY, sparklineWidth, sparklineHeight, graphBgColor);

        // Sparkline graph
        if (!dv.history.empty()) {
            std::vector<float> historyVec(dv.history.begin(), dv.history.end());

            // Find min/max for scaling
            float minVal = *std::min_element(historyVec.begin(), historyVec.end());
            float maxVal = *std::max_element(historyVec.begin(), historyVec.end());

            // Ensure some range even for constant values
            if (maxVal - minVal < 0.001f) {
                minVal -= 0.5f;
                maxVal += 0.5f;
            }

            float range = maxVal - minVal;
            float graphX = x;
            float graphBottom = sparkY + sparklineHeight;

            // Draw line segments
            for (size_t i = 1; i < historyVec.size(); i++) {
                float x1 = graphX + (i - 1) * sparklineWidth / (historyVec.size() - 1);
                float x2 = graphX + i * sparklineWidth / (historyVec.size() - 1);
                float y1 = graphBottom - ((historyVec[i-1] - minVal) / range) * sparklineHeight;
                float y2 = graphBottom - ((historyVec[i] - minVal) / range) * sparklineHeight;
                m_overlay.line(x1, y1, x2, y2, 1.5f, graphColor);
            }
        }
        x += sparklineWidth + padding;

        // Current value (use mono font for alignment)
        char buf[32];
        snprintf(buf, sizeof(buf), "%7.3f", dv.current);
        m_overlay.text(buf, x, y + ascent, color, monoFont);

        y += lineHeight;
    }
}

} // namespace vivid
