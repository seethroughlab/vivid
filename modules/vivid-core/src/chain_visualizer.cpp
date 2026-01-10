// Vivid Chain Visualizer Implementation
// Shows registered operators as nodes with connections
//
// Module-agnostic: operators provide their own visualization via drawVisualization().
// No direct dependencies on audio, render3d, or other modules.

#include <vivid/chain_visualizer.h>
#include <vivid/viz_draw_list.h>
#include <vivid/operator_viz.h>
#include <vivid/audio_operator.h>
#include <vivid/audio_graph.h>
#include <vivid/asset_loader.h>
#include <vivid/frame_input.h>
#include <vivid/effects/texture_operator.h>
#include <vivid/gui/ui_style.h>
#include <vivid/gui/gui.h>
#include "effects/font_atlas.h"
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

// -------------------------------------------------------------------------
// Color conversion helpers for color picker
// -------------------------------------------------------------------------

// RGB (0-1) to HSV (H: 0-360, S: 0-1, V: 0-1)
static void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    float maxVal = std::max({r, g, b});
    float minVal = std::min({r, g, b});
    float delta = maxVal - minVal;

    v = maxVal;
    s = (maxVal > 0.0001f) ? (delta / maxVal) : 0.0f;

    if (delta < 0.0001f) {
        h = 0.0f;
    } else if (maxVal == r) {
        h = 60.0f * std::fmod((g - b) / delta + 6.0f, 6.0f);
    } else if (maxVal == g) {
        h = 60.0f * ((b - r) / delta + 2.0f);
    } else {
        h = 60.0f * ((r - g) / delta + 4.0f);
    }
}

// HSV (H: 0-360, S: 0-1, V: 0-1) to RGB (0-1)
static void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float rp, gp, bp;
    if (h < 60.0f)       { rp = c; gp = x; bp = 0; }
    else if (h < 120.0f) { rp = x; gp = c; bp = 0; }
    else if (h < 180.0f) { rp = 0; gp = c; bp = x; }
    else if (h < 240.0f) { rp = 0; gp = x; bp = c; }
    else if (h < 300.0f) { rp = x; gp = 0; bp = c; }
    else                 { rp = c; gp = 0; bp = x; }

    r = rp + m;
    g = gp + m;
    b = bp + m;
}

// Format RGB (0-1) as hex string (#RRGGBB)
static std::string rgbToHex(float r, float g, float b) {
    int ri = static_cast<int>(std::round(r * 255.0f));
    int gi = static_cast<int>(std::round(g * 255.0f));
    int bi = static_cast<int>(std::round(b * 255.0f));
    ri = std::max(0, std::min(255, ri));
    gi = std::max(0, std::min(255, gi));
    bi = std::max(0, std::min(255, bi));
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", ri, gi, bi);
    return buf;
}

ChainVisualizer::ChainVisualizer() = default;

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
    // Save view state before entering solo
    m_preSoloZoom = m_nodeGraph.zoom();
    m_preSoloPan = m_nodeGraph.pan();

    m_soloOperator = op;
    m_soloOperatorName = name;
    m_inSoloMode = true;

    // Select this node in the graph (so inspector shows its params)
    // Find the node ID for this operator
    // Note: This is set by the caller if using double-click
}

void ChainVisualizer::exitSoloMode() {
    // Restore view state
    m_nodeGraph.setZoom(m_preSoloZoom);
    m_nodeGraph.setPan(m_preSoloPan);

    m_soloOperator = nullptr;
    m_soloOperatorName.clear();
    m_inSoloMode = false;
}

void ChainVisualizer::updateSoloOutput(vivid::Context& ctx) {
    if (!m_inSoloMode || !m_soloOperator) {
        return;
    }

    // Set the output texture to the solo operator's output
    vivid::OutputKind kind = m_soloOperator->outputKind();
    if (kind == vivid::OutputKind::Texture) {
        WGPUTextureView view = m_soloOperator->outputView();
        if (view) {
            ctx.setOutputTexture(view);
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

    // Check if chain has audio output
    bool hasAudio = ctx.chain().getAudioOutput() != nullptr;

    bool started = false;
    if (hasAudio) {
        // Start with audio muxing
        started = m_exporter.startWithAudio(outputPath, width, height, fps, codec, 48000, 2);
        if (started) {
            // Start audio recording tap (captures audio during playback)
            ctx.chain().startAudioRecordingTap();
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
    // Stop audio recording tap first
    ctx.chain().stopAudioRecordingTap();
    m_exporter.stop();
    ctx.setRecordingMode(false);
}

void ChainVisualizer::saveSnapshot(vivid::Context& ctx) {
    m_snapshotRequested = false;
    ctx.snapshot();  // Delegate to Context's snapshot method
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
    if (!m_overlay.init(ctx.device(), ctx.queue(), surfaceFormat)) {
        std::cerr << "[ChainVisualizer] Failed to initialize OverlayCanvas\n";
        return;
    }

    // Load fonts for node graph:
    // Font index 0: Inter Regular (body text, labels)
    // Font index 1: Inter Medium (node titles only)
    // Font index 2: Roboto Mono (numeric displays - FPS, timings, etc.)
    auto exeDir = AssetLoader::instance().executableDir();

    // Find project root by walking up until we find the assets folder
    // This handles both single-config (build/bin/) and multi-config (build/bin/Debug/) generators
    auto projectRoot = exeDir.parent_path().parent_path();  // build/bin -> build -> project
    if (!std::filesystem::exists(projectRoot / "modules/vivid-core/assets")) {
        // Try one more level up (for MSVC multi-config: build/bin/Debug -> build/bin -> build -> project)
        projectRoot = projectRoot.parent_path();
    }

    // Paths to font files (in modules/vivid-core/assets/fonts/)
    std::string regularPath = (projectRoot / "modules/vivid-core/assets/fonts/Inter_18pt-Regular.ttf").string();
    std::string mediumPath = (projectRoot / "modules/vivid-core/assets/fonts/Inter_18pt-Medium.ttf").string();
    std::string monoPath = (projectRoot / "modules/vivid-core/assets/fonts/RobotoMono-Regular.ttf").string();

    // Scale font sizes for HiDPI displays
    float scale = ctx.contentScale();
    if (scale < 1.0f) scale = 1.0f;

    // Load Inter Regular as primary font (index 0) - for tooltips/labels
    m_fonts[0] = std::make_unique<FontAtlas>();
    if (m_fonts[0]->load(ctx, regularPath, 16.0f * scale)) {
        m_overlay.setFont(0, m_fonts[0].get());
        std::cerr << "[ChainVisualizer] Loaded Inter Regular (" << (16 * scale) << "px)\n";
    } else {
        std::cerr << "[ChainVisualizer] Warning: Could not load Inter Regular font\n";
    }

    // Load Inter Medium for node titles (index 1)
    m_fonts[1] = std::make_unique<FontAtlas>();
    if (m_fonts[1]->load(ctx, mediumPath, 18.0f * scale)) {
        m_overlay.setFont(1, m_fonts[1].get());
        std::cerr << "[ChainVisualizer] Loaded Inter Medium (" << (18 * scale) << "px) for titles\n";
    } else {
        std::cerr << "[ChainVisualizer] Warning: Could not load Inter Medium font\n";
    }

    // Load Roboto Mono for numeric displays (index 2) - status bar
    m_fonts[2] = std::make_unique<FontAtlas>();
    if (m_fonts[2]->load(ctx, monoPath, 14.0f * scale)) {
        m_overlay.setFont(2, m_fonts[2].get());
        std::cerr << "[ChainVisualizer] Loaded Roboto Mono (" << (14 * scale) << "px) for metrics\n";
    } else {
        std::cerr << "[ChainVisualizer] Warning: Could not load Roboto Mono font\n";
    }

    // Set up callbacks for node graph interactions
    m_nodeGraph.setDoubleClickCallback([this](int nodeId) {
        m_pendingDoubleClickNodeId = nodeId;
    });

    m_nodeGraph.setOutputPinHoverCallback([this](int nodeId, int pinIndex) {
        m_hoveredOutputNodeId = nodeId;
        m_hoveredOutputPinIndex = pinIndex;
    });

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
    graphInput.time = input.time;

    // Begin overlay rendering
    m_overlay.begin(input.width, input.height);

    // Check if mouse is in inspector panel area (block node graph panning if so)
    // But don't block if the mouse is in the mini-map (which is in bottom-right)
    bool blockNodeGraphInput = m_activeDrag.active;
    if (!blockNodeGraphInput && m_inspectorVisible && m_inspectorBounds.valid) {
        // Use tracked inspector bounds for accurate hit testing
        if (scaledMousePos.x >= m_inspectorBounds.x &&
            scaledMousePos.x <= m_inspectorBounds.x + m_inspectorBounds.w &&
            scaledMousePos.y >= m_inspectorBounds.y &&
            scaledMousePos.y <= m_inspectorBounds.y + m_inspectorBounds.h) {
            // Don't block if in mini-map area
            if (!m_nodeGraph.isPointInMiniMap(scaledMousePos)) {
                blockNodeGraphInput = true;
            }
        }
    }

    // Create modified input for node graph (block all mouse events if in inspector area)
    vivid::NodeGraphInput nodeGraphInput = graphInput;
    if (blockNodeGraphInput) {
        nodeGraphInput.mouseClicked[0] = false;
        nodeGraphInput.mouseDown[0] = false;
        nodeGraphInput.mouseReleased[0] = false;  // Block release too, prevents selection clearing
        nodeGraphInput.scroll = {0, 0};  // Block scroll to prevent zoom while scrolling inspector
    }

    // Skip node graph rendering when in solo mode (but keep inspector)
    bool renderNodeGraph = !m_inSoloMode;

    if (renderNodeGraph) {
    // Begin node graph editor
    m_nodeGraph.beginEditor(m_overlay, static_cast<float>(input.width), static_cast<float>(input.height), nodeGraphInput);

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

        // Check if this operator has value bindings (for dashed link visualization)
        // If so, add a "values" input pin for the connections
        auto paramDecls = info.op->params();
        bool hasValueBindings = false;
        for (const auto& p : paramDecls) {
            if (p.boundOperator) {
                hasValueBindings = true;
                break;
            }
        }
        if (hasValueBindings) {
            // Use pin ID at slot after all potential inputs (up to 10 texture inputs)
            int valuePinId = nodeId * 100 + 50;  // Offset to avoid texture input pins
            m_nodeGraph.beginInputAttribute(valuePinId);
            m_nodeGraph.pinLabel("values");
            m_nodeGraph.endInputAttribute();
        }

        // Check if this operator has a trigger source (for dashed link visualization)
        if (info.op->triggerSource()) {
            int trigPinId = nodeId * 100 + 51;  // Offset 51 for trigger pins
            m_nodeGraph.beginInputAttribute(trigPinId);
            m_nodeGraph.pinLabel("trig");
            m_nodeGraph.endInputAttribute();
        }

        // Check if this operator has an event source (for dashed link visualization)
        if (info.op->eventSource()) {
            int evtPinId = nodeId * 100 + 52;  // Offset 52 for event pins
            m_nodeGraph.beginInputAttribute(evtPinId);
            m_nodeGraph.pinLabel("evt");
            m_nodeGraph.endInputAttribute();
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

    // Add dashed links for value operator bindings (LFO -> param, etc.)
    // These show as orange dashed lines in the visualizer
    glm::vec4 valueBindingColor = {1.0f, 0.7f, 0.3f, 0.9f};  // Orange for value connections
    for (size_t i = 0; i < operators.size(); ++i) {
        const vivid::OperatorInfo& info = operators[i];
        if (!info.op) continue;

        int dstNodeId = static_cast<int>(i);

        // Query params for value bindings
        auto paramDecls = info.op->params();
        for (const auto& param : paramDecls) {
            if (!param.boundOperator) continue;

            // Find source node for the bound operator
            for (size_t k = 0; k < operators.size(); ++k) {
                if (operators[k].op == param.boundOperator) {
                    int srcNodeId = static_cast<int>(k);
                    // Connect to the "values" input pin (offset 50)
                    int srcOutputPinId = srcNodeId * 100;
                    int dstInputPinId = dstNodeId * 100 + 50;  // "values" input pin
                    m_nodeGraph.linkDashed(linkId++, srcOutputPinId, dstInputPinId, valueBindingColor);
                    break;
                }
            }
        }
    }

    // Add dashed links for trigger connections (parallel to value bindings above)
    glm::vec4 triggerColor = {0.4f, 0.8f, 1.0f, 0.9f};  // Cyan for trigger connections
    for (size_t i = 0; i < operators.size(); ++i) {
        const vivid::OperatorInfo& info = operators[i];
        if (!info.op) continue;

        vivid::Operator* triggerSrc = info.op->triggerSource();
        if (!triggerSrc) continue;

        int dstNodeId = static_cast<int>(i);

        // Find source node for the trigger operator
        for (size_t k = 0; k < operators.size(); ++k) {
            if (operators[k].op == triggerSrc) {
                int srcNodeId = static_cast<int>(k);
                // Connect to the "trig" input pin (offset 51)
                int srcOutputPinId = srcNodeId * 100;
                int dstInputPinId = dstNodeId * 100 + 51;
                m_nodeGraph.linkDashed(linkId++, srcOutputPinId, dstInputPinId, triggerColor);
                break;
            }
        }
    }

    // Add dashed links for event connections
    glm::vec4 eventColor = {0.4f, 1.0f, 0.6f, 0.9f};  // Green for event connections
    for (size_t i = 0; i < operators.size(); ++i) {
        const vivid::OperatorInfo& info = operators[i];
        if (!info.op) continue;

        vivid::Operator* eventSrc = info.op->eventSource();
        if (!eventSrc) continue;

        int dstNodeId = static_cast<int>(i);

        // Find source node for the event operator
        for (size_t k = 0; k < operators.size(); ++k) {
            if (operators[k].op == eventSrc) {
                int srcNodeId = static_cast<int>(k);
                // Connect to the "evt" input pin (offset 52)
                int srcOutputPinId = srcNodeId * 100;
                int dstInputPinId = dstNodeId * 100 + 52;
                m_nodeGraph.linkDashed(linkId++, srcOutputPinId, dstInputPinId, eventColor);
                break;
            }
        }
    }

    // Do hierarchical layout using Sugiyama algorithm (with crossing reduction)
    // Reset layout if operator count changes (chain was hot-reloaded)
    if (operators.size() != m_lastOperatorCount) {
        m_autoLayoutDone = false;
        m_lastOperatorCount = operators.size();
    }
    if (!m_autoLayoutDone && !operators.empty()) {
        m_nodeGraph.autoLayout();
        m_nodeGraph.zoomToFit();
        m_autoLayoutDone = true;
    }

    // End node graph editor
    m_nodeGraph.endEditor();
    } // end if (renderNodeGraph)

    // Render status bar (in screen space, not node graph space)
    // Hidden in solo mode to maximize output visibility
    m_overlay.resetTransform();
    if (!m_inSoloMode) {
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
                } else if (isMouseInRect(m_gridToggleButton, mousePos)) {
                    // Toggle grid visibility
                    m_nodeGraph.style().showGrid = !m_nodeGraph.style().showGrid;
                }
            }
        }
    }

    // Render output pin tooltip (lightweight: size, format only)
    if (!m_inSoloMode && m_hoveredOutputNodeId >= 0 && m_hoveredOutputPinIndex >= 0) {
        if (static_cast<size_t>(m_hoveredOutputNodeId) < operators.size()) {
            const vivid::OperatorInfo& info = operators[m_hoveredOutputNodeId];
            if (info.op) {
                renderOutputPinTooltip(input, info);
            }
        }
    }

    // Render debug values panel (bottom-left corner)
    renderDebugPanelOverlay(input, ctx);

    // Render inspector panel (right side, shows selected node's parameters)
    renderInspectorPanel(input, ctx);

    // Handle keyboard shortcuts (using new key input system)
    using vivid::Key;

    // Handle double-click to enter solo mode
    if (m_pendingDoubleClickNodeId >= 0) {
        int nodeId = m_pendingDoubleClickNodeId;
        m_pendingDoubleClickNodeId = -1;  // Clear pending

        if (nodeId != SCREEN_NODE_ID && nodeId != SPEAKERS_NODE_ID) {
            if (static_cast<size_t>(nodeId) < operators.size()) {
                const vivid::OperatorInfo& info = operators[nodeId];
                if (info.op) {
                    // Select this node so inspector shows its params
                    m_nodeGraph.selectNode(nodeId);
                    m_selectedNodeId = nodeId;
                    m_selectedOp = info.op;
                    m_selectedOpName = info.name;
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
    // Note: Output texture is set by updateSoloOutput() before blit
    if (m_inSoloMode && m_soloOperator) {
        // Draw solo indicator with close button
        renderSoloIndicator(input);

        // Handle close button click
        if (graphInput.mouseClicked[0] && isMouseInRect(m_soloCloseButton, graphInput.mousePos)) {
            exitSoloMode();
        }
    }

    // Render the overlay
    m_overlay.render(pass);
}

void ChainVisualizer::renderStatusBar(const FrameInput& input, vivid::Context& ctx) {
    // Draw status bar on its own layer (below Panels content to avoid clipping)
    m_overlay.setLayer(UILayer::Panels - 2);

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

    // Pending changes indicator (Claude-first workflow)
    if (m_pendingChangeCount > 0) {
        // Separator
        m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
        x += padding * 2;

        snprintf(buf, sizeof(buf), "Pending: %zu", m_pendingChangeCount);
        m_overlay.text(buf, x, y, yellowColor, monoFont);
        x += m_overlay.measureText(buf, monoFont) + padding * 2;
    }

    // MCP configuration warning
    if (!m_mcpWarning.empty()) {
        // Separator
        m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
        x += padding * 2;

        m_overlay.text(m_mcpWarning, x, y, redColor, monoFont);
        x += m_overlay.measureText(m_mcpWarning, monoFont) + padding * 2;
    }

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

    // Grid toggle (between stats and recording controls)
    {
        // Separator
        m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
        x += padding * 2;

        // Checkbox
        float checkSize = lineH * 0.7f;
        float checkY = (barHeight - checkSize) * 0.5f;
        glm::vec4 checkBg = {0.2f, 0.2f, 0.25f, 1.0f};
        glm::vec4 checkBorder = {0.4f, 0.4f, 0.45f, 1.0f};
        glm::vec4 checkFill = {0.4f, 0.7f, 0.9f, 1.0f};

        m_overlay.fillRect(x, checkY, checkSize, checkSize, checkBg);
        m_overlay.strokeRect(x, checkY, checkSize, checkSize, 1, checkBorder);

        // Draw checkmark if grid is enabled
        if (m_nodeGraph.style().showGrid) {
            // Simple checkmark: two lines forming a check
            float cx = x + checkSize * 0.5f;
            float cy = checkY + checkSize * 0.5f;
            float s = checkSize * 0.3f;
            m_overlay.line(cx - s, cy, cx - s * 0.3f, cy + s * 0.7f, 2.0f, checkFill);
            m_overlay.line(cx - s * 0.3f, cy + s * 0.7f, cx + s, cy - s * 0.5f, 2.0f, checkFill);
        }

        float checkboxWidth = checkSize;
        x += checkboxWidth + 4;

        // Label
        m_overlay.text("Grid", x, y, textColor, monoFont);
        float labelWidth = m_overlay.measureText("Grid", monoFont);

        // Store button region (checkbox + label)
        float totalWidth = checkboxWidth + 4 + labelWidth;
        m_gridToggleButton = {x - checkboxWidth - 4, checkY, totalWidth, checkSize, true};

        x += labelWidth + padding * 2;
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
            // Dropdown menus render on menus layer (above panels)
            m_overlay.setLayer(UILayer::Menus);

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
            m_overlay.fillRoundedRect(menuX, menuY, menuWidth, menuH, 4, menuBg);
            m_overlay.strokeRoundedRect(menuX, menuY, menuWidth, menuH, 4, 1, buttonBorder);

            // Menu items
            float itemY = menuY;
            m_codecH264 = {menuX, itemY, menuWidth, itemH, true};
            m_overlay.text(items[0], menuX + buttonPadX, itemY + buttonPadY + ascent, textColor, monoFont);

            itemY += itemH;
            m_codecH265 = {menuX, itemY, menuWidth, itemH, true};
            m_overlay.text(items[1], menuX + buttonPadX, itemY + buttonPadY + ascent, textColor, monoFont);

            itemY += itemH;
            m_codecProRes = {menuX, itemY, menuWidth, itemH, true};
            m_overlay.text(items[2], menuX + buttonPadX, itemY + buttonPadY + ascent, textColor, monoFont);
        }
    }
}

void ChainVisualizer::renderOutputPinTooltip(const FrameInput& input, const vivid::OperatorInfo& info) {
    if (!info.op) return;

    // Use font 0 (Inter Regular) metrics for tooltip
    float lineH = m_overlay.fontLineHeight(0);
    float ascent = m_overlay.fontAscent(0);
    if (lineH <= 0) lineH = 22.0f;
    if (ascent <= 0) ascent = 16.0f;

    const float padding = 6.0f;

    // Colors
    glm::vec4 bgColor = {0.1f, 0.1f, 0.12f, 0.95f};
    glm::vec4 borderColor = {0.4f, 0.4f, 0.45f, 1.0f};
    glm::vec4 textColor = {0.9f, 0.9f, 0.9f, 1.0f};

    // Build minimal output info
    std::string tooltipText;
    vivid::OutputKind kind = info.op->outputKind();

    if (kind == vivid::OutputKind::Texture) {
        WGPUTexture tex = info.op->outputTexture();
        if (tex) {
            uint32_t w = wgpuTextureGetWidth(tex);
            uint32_t h = wgpuTextureGetHeight(tex);
            WGPUTextureFormat fmt = wgpuTextureGetFormat(tex);
            const char* fmtStr = "RGBA16F";  // Default for EFFECTS_FORMAT
            if (fmt == WGPUTextureFormat_BGRA8Unorm) fmtStr = "BGRA8";
            else if (fmt == WGPUTextureFormat_RGBA8Unorm) fmtStr = "RGBA8";
            else if (fmt == WGPUTextureFormat_BGRA8UnormSrgb) fmtStr = "BGRA8sRGB";
            else if (fmt == WGPUTextureFormat_RGBA8UnormSrgb) fmtStr = "RGBA8sRGB";
            else if (fmt == WGPUTextureFormat_RGBA32Float) fmtStr = "RGBA32F";

            char buf[64];
            snprintf(buf, sizeof(buf), "%ux%u %s", w, h, fmtStr);
            tooltipText = buf;
        } else {
            tooltipText = "No texture";
        }
    } else if (kind == vivid::OutputKind::Audio) {
        // TODO: Get actual audio stats from operator
        tooltipText = "48000 Hz, stereo, 512 samples";
    } else if (kind == vivid::OutputKind::Value) {
        tooltipText = "Float value";
    } else if (kind == vivid::OutputKind::Geometry) {
        tooltipText = "3D Geometry";
    } else {
        tooltipText = vivid::outputKindName(kind);
    }

    // Calculate size
    float tooltipWidth = m_overlay.measureText(tooltipText) + padding * 2;
    float tooltipHeight = lineH + padding * 2;

    // Position near mouse
    float mouseX = input.mousePos.x * (input.contentScale > 0 ? input.contentScale : 1.0f);
    float mouseY = input.mousePos.y * (input.contentScale > 0 ? input.contentScale : 1.0f);
    float tooltipX = mouseX + 12;
    float tooltipY = mouseY + 12;

    // Keep on screen
    if (tooltipX + tooltipWidth > input.width) {
        tooltipX = mouseX - tooltipWidth - 8;
    }
    if (tooltipY + tooltipHeight > input.height) {
        tooltipY = mouseY - tooltipHeight - 8;
    }

    // Draw tooltip
    m_overlay.setLayer(UILayer::Tooltips);
    m_overlay.fillRoundedRect(tooltipX, tooltipY, tooltipWidth, tooltipHeight, 3.0f, bgColor);
    m_overlay.strokeRoundedRect(tooltipX, tooltipY, tooltipWidth, tooltipHeight, 3.0f, 1.0f, borderColor);
    m_overlay.text(tooltipText, tooltipX + padding, tooltipY + padding + ascent, textColor);
}

void ChainVisualizer::renderSoloIndicator(const FrameInput& input) {
    if (!m_inSoloMode || !m_soloOperator) return;

    m_overlay.setLayer(UILayer::Tooltips);

    float lineH = m_overlay.fontLineHeight(0);
    float ascent = m_overlay.fontAscent(0);
    if (lineH <= 0) lineH = 22.0f;
    if (ascent <= 0) ascent = 16.0f;

    const float padding = 10.0f;
    const float closeButtonSize = lineH;

    std::string soloText = "SOLO: " + m_soloOperatorName;
    float textWidth = m_overlay.measureText(soloText, 0);
    float boxWidth = textWidth + padding * 3 + closeButtonSize;
    float boxHeight = lineH + padding * 2;

    glm::vec4 bgColor = {0.15f, 0.12f, 0.05f, 0.95f};
    glm::vec4 borderColor = {0.8f, 0.6f, 0.2f, 1.0f};
    glm::vec4 soloColor = {1.0f, 0.9f, 0.4f, 1.0f};
    glm::vec4 closeColor = {0.7f, 0.7f, 0.7f, 1.0f};
    glm::vec4 closeHoverColor = {1.0f, 0.4f, 0.4f, 1.0f};

    float boxX = padding;
    float boxY = padding;

    // Draw background
    m_overlay.fillRoundedRect(boxX, boxY, boxWidth, boxHeight, 4.0f, bgColor);
    m_overlay.strokeRoundedRect(boxX, boxY, boxWidth, boxHeight, 4.0f, 1.0f, borderColor);

    // Draw "SOLO: name" text
    m_overlay.text(soloText, boxX + padding, boxY + padding + ascent, soloColor);

    // Draw close button (×)
    float closeX = boxX + boxWidth - padding - closeButtonSize;
    float closeY = boxY + padding;

    // Store close button rect for click detection
    m_soloCloseButton = {closeX, closeY, closeButtonSize, closeButtonSize, true};

    // Check if mouse is hovering the close button
    float mouseX = input.mousePos.x * (input.contentScale > 0 ? input.contentScale : 1.0f);
    float mouseY = input.mousePos.y * (input.contentScale > 0 ? input.contentScale : 1.0f);
    bool hovering = mouseX >= closeX && mouseX <= closeX + closeButtonSize &&
                    mouseY >= closeY && mouseY <= closeY + closeButtonSize;

    glm::vec4 xColor = hovering ? closeHoverColor : closeColor;

    // Draw × symbol centered in the button area
    float xCenterX = closeX + closeButtonSize / 2;
    float xCenterY = closeY + closeButtonSize / 2;
    float xSize = closeButtonSize * 0.3f;

    m_overlay.line(xCenterX - xSize, xCenterY - xSize, xCenterX + xSize, xCenterY + xSize, 2.0f, xColor);
    m_overlay.line(xCenterX + xSize, xCenterY - xSize, xCenterX - xSize, xCenterY + xSize, 2.0f, xColor);
}

void ChainVisualizer::renderInspectorPanel(const FrameInput& input, vivid::Context& ctx) {
    if (!m_inspectorVisible) {
        m_inspectorBounds.valid = false;
        return;
    }

    // Determine which operator to inspect based on selected node
    int selectedNodeId = m_nodeGraph.getSelectedNode();
    vivid::Operator* op = nullptr;
    std::string title;

    if (selectedNodeId == SPEAKERS_NODE_ID) {
        op = ctx.hasChain() ? ctx.chain().getAudioOutput() : nullptr;
        title = "Speakers";
    } else if (selectedNodeId == SCREEN_NODE_ID) {
        // Screen node shows display settings
        renderScreenInspector(input, ctx);
        return;
    } else if (selectedNodeId >= 0) {
        const auto& operators = ctx.registeredOperators();
        if (static_cast<size_t>(selectedNodeId) < operators.size()) {
            op = operators[selectedNodeId].op;
            title = operators[selectedNodeId].name;
        }
    }

    if (!op) {
        m_inspectorScrollOffset = 0.0f;
        m_inspectorBounds.valid = false;
        return;
    }

    renderOperatorInspector(input, op, title);
}

void ChainVisualizer::renderOperatorInspector(const FrameInput& input, vivid::Operator* op, const std::string& title) {
    // Get parameters
    auto params = op->params();
    if (params.empty()) {
        m_inspectorBounds.valid = false;
        return;
    }

    // Inspector panel renders on Panels layer (above nodes/thumbnails)
    m_overlay.setLayer(UILayer::Panels);

    // Font metrics
    const int labelFont = 0;  // Inter Regular
    const int monoFont = 2;   // Roboto Mono
    float lineH = m_overlay.fontLineHeight(labelFont);
    float ascent = m_overlay.fontAscent(labelFont);
    if (lineH <= 0) lineH = 20.0f;
    if (ascent <= 0) ascent = 14.0f;

    // Scale layout values for HiDPI
    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;
    float inspectorWidth = m_inspectorWidth * scale;  // Scale the panel width

    // Create scaled FrameInput for Gui widgets (HiDPI support)
    FrameInput scaledInput = input;
    scaledInput.mousePos *= scale;

    // Create Gui instance with inspector-compatible style
    Gui gui(m_overlay, scaledInput);
    gui.style().labelPosition = LabelPosition::Above;
    gui.style().valuePosition = ValuePosition::Right;
    gui.style().padding = 12.0f * scale;
    gui.style().widgetHeight = 20.0f * scale;
    gui.style().valueWidth = 60.0f * scale;
    gui.style().cornerRadius = 3.0f * scale;
    gui.style().widgetBackground = {0.2f, 0.2f, 0.25f, 1.0f};
    gui.style().sliderFill = {0.4f, 0.6f, 0.9f, 1.0f};
    gui.style().sliderFillActive = {0.5f, 0.7f, 1.0f, 1.0f};
    gui.style().textDim = {0.5f, 0.5f, 0.55f, 1.0f};

    // Layout (scaled for HiDPI)
    const float padding = 12.0f * scale;
    const float sliderHeight = 20.0f * scale;
    const float rowHeight = lineH + sliderHeight + 4.0f * scale;  // Label + slider + spacing
    const float headerHeight = lineH + padding * 2;

    // Calculate total content height based on parameters
    float totalRowsHeight = 0;
    for (const auto& p : params) {
        switch (p.type) {
            case ParamType::Vec2:
                // XY pad takes ~3.5 row heights (label + square pad)
                totalRowsHeight += rowHeight * 2.5f;
                break;
            case ParamType::Vec3:
                // Compact row takes ~2 row heights (label + component labels + sliders)
                totalRowsHeight += rowHeight * 2.0f;
                break;
            case ParamType::Vec4:
            case ParamType::Color: totalRowsHeight += rowHeight * 4; break;
            case ParamType::Enum: totalRowsHeight += rowHeight; break;  // Dropdown is single row
            case ParamType::DeviceList: totalRowsHeight += rowHeight; break;  // Dynamic dropdown is single row
            case ParamType::ADSR: totalRowsHeight += rowHeight * 4; break;  // Graph + 4 mini-sliders
            default: totalRowsHeight += rowHeight; break;
        }
    }
    m_inspectorContentHeight = totalRowsHeight + padding * 2;

    // Panel position (right side, below status bar)
    float statusBarHeight = lineH + 12.0f * scale;
    float panelX = input.width - inspectorWidth - padding;
    float panelY = statusBarHeight + padding;

    // Calculate visible area height (clamped to screen, accounting for mini map)
    // Mini map is in bottom-right corner: height=150, margin=16 (from NodeGraphStyle defaults)
    float miniMapReservedHeight = (150.0f + 16.0f + padding) * scale;
    float maxPanelHeight = input.height - panelY - miniMapReservedHeight;
    float contentAreaHeight = maxPanelHeight - headerHeight;  // Visible content area
    float panelHeight = std::min(headerHeight + m_inspectorContentHeight, maxPanelHeight);

    // Store bounds for input blocking (used in renderNodeGraph)
    m_inspectorBounds.x = panelX;
    m_inspectorBounds.y = panelY;
    m_inspectorBounds.w = inspectorWidth;
    m_inspectorBounds.h = panelHeight;
    m_inspectorBounds.valid = true;

    // Handle scroll input (UI-only, no GPU operations)
    // Only scroll if mouse is in the panel area
    glm::vec2 mousePos = input.mousePos * scale;
    bool mouseInPanel = mousePos.x >= panelX && mousePos.x <= panelX + inspectorWidth &&
                        mousePos.y >= panelY && mousePos.y <= panelY + panelHeight;

    if (mouseInPanel && (input.scroll.y != 0.0f)) {
        m_inspectorScrollOffset -= input.scroll.y * 30.0f * scale;
        float maxScroll = std::max(0.0f, m_inspectorContentHeight - contentAreaHeight);
        m_inspectorScrollOffset = std::max(0.0f, std::min(m_inspectorScrollOffset, maxScroll));
    }

    // Colors
    glm::vec4 bgColor = {0.12f, 0.12f, 0.15f, 0.95f};
    glm::vec4 headerBg = {0.16f, 0.16f, 0.2f, 1.0f};
    glm::vec4 borderColor = {0.3f, 0.3f, 0.35f, 1.0f};
    glm::vec4 titleColor = {0.5f, 0.8f, 1.0f, 1.0f};
    glm::vec4 textColor = {0.85f, 0.85f, 0.85f, 1.0f};
    glm::vec4 dimColor = {0.5f, 0.5f, 0.55f, 1.0f};
    glm::vec4 sliderBg = {0.2f, 0.2f, 0.25f, 1.0f};
    glm::vec4 sliderFill = {0.4f, 0.6f, 0.9f, 1.0f};
    glm::vec4 sliderActive = {0.5f, 0.7f, 1.0f, 1.0f};

    // Draw panel background on a layer below the content (so it's not clipped)
    m_overlay.setLayer(UILayer::Panels - 1);  // Panel background layer
    float cornerRadius = 6.0f * scale;
    m_overlay.fillRoundedRect(panelX, panelY, inspectorWidth, panelHeight, cornerRadius, bgColor);
    m_overlay.strokeRoundedRect(panelX, panelY, inspectorWidth, panelHeight, cornerRadius, 1.0f * scale, borderColor);

    // Header (use fillRoundedRectTop so top corners match the panel's rounded corners)
    m_overlay.fillRoundedRectTop(panelX, panelY, inspectorWidth, headerHeight, cornerRadius, headerBg);
    std::string headerTitle = op->name() + " (" + title + ")";
    m_overlay.text(headerTitle, panelX + padding, panelY + padding + ascent, titleColor, labelFont);

    // Switch to main Panels layer for content (this layer will be clipped)
    m_overlay.setLayer(UILayer::Panels);

    // Mouse state (mousePos and scale already calculated above for scroll)
    bool mouseDown = input.mouseDown[0];
    static bool lastMouseDown = false;
    bool mouseClicked = mouseDown && !lastMouseDown;
    bool mouseReleased = !mouseDown && lastMouseDown;
    lastMouseDown = mouseDown;

    // Visible content bounds (for visibility culling)
    float visibleTop = panelY + headerHeight;
    float visibleBottom = panelY + panelHeight;
    float sliderWidth = inspectorWidth - padding * 4 - 60.0f * scale;  // Leave room for value label

    // Content area - apply scroll offset (UI coordinate only)
    float contentY = panelY + headerHeight + padding - m_inspectorScrollOffset;

    // Set up Gui content area for widgets
    float contentAreaX = panelX + padding;
    float contentAreaW = inspectorWidth - padding * 2;
    gui.beginArea(contentAreaX, visibleTop, contentAreaW, contentAreaHeight);

    // Set layer clip rect to clip content to panel bounds
    // This applies scissor rect when rendering the Panels layer
    m_overlay.setLayerClipRect(UILayer::Panels, panelX, visibleTop, inspectorWidth, contentAreaHeight);

    for (const auto& p : params) {
        float value[4] = {0};
        op->getParam(p.name, value);

        int componentCount = 1;
        const char* componentLabels[] = {"", "", "", ""};

        switch (p.type) {
            case ParamType::Vec2:
                componentCount = 2;
                componentLabels[0] = "X"; componentLabels[1] = "Y";
                break;
            case ParamType::Vec3:
                componentCount = 3;
                componentLabels[0] = "X"; componentLabels[1] = "Y"; componentLabels[2] = "Z";
                break;
            case ParamType::Vec4:
                componentCount = 4;
                componentLabels[0] = "X"; componentLabels[1] = "Y";
                componentLabels[2] = "Z"; componentLabels[3] = "W";
                break;
            case ParamType::Color:
                componentCount = 4;
                componentLabels[0] = "R"; componentLabels[1] = "G";
                componentLabels[2] = "B"; componentLabels[3] = "A";
                break;
            case ParamType::Enum:
            case ParamType::DeviceList:
                // Handled separately below
                break;
            default:
                break;
        }

        // Handle Enum type with dropdown - use Gui dropdown
        if (p.type == ParamType::Enum) {
            float y = contentY;
            float itemBottom = y + rowHeight;
            bool isVisible = (itemBottom > visibleTop) && (y < visibleBottom);

            if (isVisible && !p.enumLabels.empty()) {
                // Position Gui cursor
                gui.setCursorY(y);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                // Get current index and track previous for callback
                int currentIndex = static_cast<int>(value[0]);
                int prevIndex = currentIndex;

                // Use Gui dropdown
                if (gui.dropdown(p.name.c_str(), &currentIndex, p.enumLabels)) {
                    // Selection changed - update operator
                    float newValue[4] = {static_cast<float>(currentIndex), 0, 0, 0};
                    op->setParam(p.name, newValue);

                    // Fire change callback
                    if (m_paramChangeCallback) {
                        float oldValue[4] = {static_cast<float>(prevIndex), 0, 0, 0};
                        m_paramChangeCallback(title, p.name, oldValue, newValue, op->sourceLine);
                    }
                }

                gui.popId();
                gui.popId();
            }

            contentY += rowHeight;
            continue;  // Skip the slider rendering loop
        }

        // Handle DeviceList type with dynamic dropdown - use Gui dropdown
        if (p.type == ParamType::DeviceList) {
            float y = contentY;
            float itemBottom = y + rowHeight;
            bool isVisible = (itemBottom > visibleTop) && (y < visibleBottom);

            if (isVisible && p.deviceListProvider) {
                // Position Gui cursor
                gui.setCursorY(y);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                // Get dynamic device list
                std::vector<std::string> deviceList = p.deviceListProvider();

                // Get current index and track previous for callback
                int currentIndex = static_cast<int>(value[0]);
                int prevIndex = currentIndex;

                // Clamp to valid range
                if (currentIndex < 0) currentIndex = 0;
                if (currentIndex >= static_cast<int>(deviceList.size())) {
                    currentIndex = 0;
                }

                // Use Gui dropdown
                if (gui.dropdown(p.name.c_str(), &currentIndex, deviceList)) {
                    // Selection changed - update operator
                    float newValue[4] = {static_cast<float>(currentIndex), 0, 0, 0};
                    op->setParam(p.name, newValue);

                    // Fire change callback
                    if (m_paramChangeCallback) {
                        float oldValue[4] = {static_cast<float>(prevIndex), 0, 0, 0};
                        m_paramChangeCallback(title, p.name, oldValue, newValue, op->sourceLine);
                    }
                }

                gui.popId();
                gui.popId();
            }

            contentY += rowHeight;
            continue;  // Skip the slider rendering loop
        }

        // Handle Color type with HSV color picker - use Gui
        if (p.type == ParamType::Color) {
            float y = contentY;
            float itemBottom = y + rowHeight;
            bool isVisible = (itemBottom > visibleTop) && (y < visibleBottom);

            if (isVisible) {
                // Position Gui cursor
                gui.setCursorY(y);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                // Check if THIS color picker is expanded
                bool isExpanded = m_colorPicker.expanded &&
                                 m_colorPicker.operatorName == title &&
                                 m_colorPicker.paramName == p.name;
                bool wasExpanded = isExpanded;

                // Get color as vec4
                glm::vec4 color = {value[0], value[1], value[2], value[3]};

                // Use Gui color picker
                auto result = gui.colorPickerHSV(p.name.c_str(), &color, &isExpanded);

                // Handle expand/collapse state change
                if (isExpanded != wasExpanded) {
                    if (isExpanded) {
                        m_colorPicker.expanded = true;
                        m_colorPicker.operatorName = title;
                        m_colorPicker.paramName = p.name;
                        for (int i = 0; i < 4; ++i) {
                            m_colorPicker.originalColor[i] = value[i];
                        }
                    } else {
                        m_colorPicker.expanded = false;
                    }
                }

                // On drag start: capture context for callback
                if (result.dragStarted) {
                    m_activeDrag.operatorName = title;
                    m_activeDrag.paramName = p.name;
                    m_activeDrag.sourceLine = op->sourceLine;
                    m_activeDrag.originalValue[0] = result.startColor.r;
                    m_activeDrag.originalValue[1] = result.startColor.g;
                    m_activeDrag.originalValue[2] = result.startColor.b;
                    m_activeDrag.originalValue[3] = result.startColor.a;
                    m_activeDrag.active = true;
                }

                // On value change: update operator
                if (result.changed) {
                    float newValues[4] = {color.r, color.g, color.b, color.a};
                    op->setParam(p.name, newValues);
                }

                // On drag end: fire callback
                if (result.dragEnded && m_activeDrag.active &&
                    m_activeDrag.operatorName == title &&
                    m_activeDrag.paramName == p.name) {
                    float newValues[4] = {color.r, color.g, color.b, color.a};
                    if (m_paramChangeCallback) {
                        m_paramChangeCallback(m_activeDrag.operatorName, m_activeDrag.paramName,
                                              m_activeDrag.originalValue, newValues,
                                              m_activeDrag.sourceLine);
                    }
                    m_activeDrag.active = false;
                }

                gui.popId();
                gui.popId();
            }

            // Advance content by appropriate height
            float swatchHeight = sliderHeight + lineH + 8.0f * scale;
            bool isExpanded = m_colorPicker.expanded &&
                             m_colorPicker.operatorName == title &&
                             m_colorPicker.paramName == p.name;
            contentY += swatchHeight;
            if (isExpanded) {
                contentY += 4 * rowHeight;
            }

            continue;  // Skip the normal slider rendering loop
        }

        // ADSR envelope type - use Gui ADSR widget
        if (p.type == ParamType::ADSR) {
            float y = contentY;
            float adsrHeight = rowHeight * 4;  // Match height calculation above
            float itemBottom = y + adsrHeight;
            bool isVisible = (itemBottom > visibleTop) && (y < visibleBottom);

            if (isVisible) {
                // Position Gui cursor
                gui.setCursorY(y);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                // Get ADSR values: value[0]=attack, value[1]=decay, value[2]=sustain, value[3]=release
                float attack = value[0];
                float decay = value[1];
                float sustain = value[2];
                float release = value[3];

                // Use Gui ADSR widget
                auto result = gui.adsrEnvelope(p.name.c_str(), &attack, &decay, &sustain, &release, p.maxVal);

                // On drag start: capture context for callback
                if (result.dragStarted) {
                    m_activeDrag.operatorName = title;
                    m_activeDrag.paramName = p.name;
                    m_activeDrag.sourceLine = op->sourceLine;
                    m_activeDrag.originalValue[0] = result.startA;
                    m_activeDrag.originalValue[1] = result.startD;
                    m_activeDrag.originalValue[2] = result.startS;
                    m_activeDrag.originalValue[3] = result.startR;
                    m_activeDrag.active = true;
                }

                // On value change: update operator
                if (result.changed) {
                    float newValues[4] = {attack, decay, sustain, release};
                    op->setParam(p.name, newValues);
                }

                // On drag end: fire callback
                if (result.dragEnded && m_activeDrag.active &&
                    m_activeDrag.operatorName == title &&
                    m_activeDrag.paramName == p.name) {
                    float newValues[4] = {attack, decay, sustain, release};
                    if (m_paramChangeCallback) {
                        m_paramChangeCallback(m_activeDrag.operatorName, m_activeDrag.paramName,
                                              m_activeDrag.originalValue, newValues,
                                              m_activeDrag.sourceLine);
                    }
                    m_activeDrag.active = false;
                }

                gui.popId();
                gui.popId();
            }

            contentY += adsrHeight;
            continue;  // Skip the normal slider rendering loop
        }

        // Simple params (Float/Int/Bool) - use Gui slider
        if (componentCount == 1) {
            float y = contentY;
            float itemBottom = y + rowHeight;
            bool isVisible = (itemBottom > visibleTop) && (y < visibleBottom);

            if (isVisible) {
                // Position Gui cursor and use pushId for scoping
                gui.setCursorY(y);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                // Use sliderEx to get drag state
                auto result = gui.sliderEx(p.name.c_str(), &value[0], p.minVal, p.maxVal);

                // On drag start: capture context for callback
                if (result.dragStarted) {
                    m_activeDrag.operatorName = title;
                    m_activeDrag.paramName = p.name;
                    m_activeDrag.sourceLine = op->sourceLine;
                    for (int i = 0; i < 4; ++i) {
                        m_activeDrag.originalValue[i] = value[i];
                    }
                    m_activeDrag.active = true;
                }

                // On value change: update operator
                if (result.changed) {
                    float newValues[4] = {value[0], 0, 0, 0};
                    op->setParam(p.name, newValues);
                }

                // On drag end: fire callback
                if (result.dragEnded && m_activeDrag.active &&
                    m_activeDrag.operatorName == title &&
                    m_activeDrag.paramName == p.name) {
                    float newValues[4] = {value[0], 0, 0, 0};
                    if (m_paramChangeCallback) {
                        m_paramChangeCallback(m_activeDrag.operatorName, m_activeDrag.paramName,
                                              m_activeDrag.originalValue, newValues,
                                              m_activeDrag.sourceLine);
                    }
                    m_activeDrag.active = false;
                }

                gui.popId();
                gui.popId();
            }

            contentY += rowHeight;
            continue;
        }

        // Vec2 params - use XY pad
        if (p.type == ParamType::Vec2) {
            float y = contentY;
            float padHeight = rowHeight * 2.5f;  // Match height calculation above
            float itemBottom = y + padHeight;
            bool isVisible = (itemBottom > visibleTop) && (y < visibleBottom);

            if (isVisible) {
                gui.setCursorY(y);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                glm::vec2 vec2Value = {value[0], value[1]};
                auto result = gui.xyPadEx(p.name.c_str(), &vec2Value,
                                          p.minVal, p.maxVal, p.minVal, p.maxVal, 0);

                if (result.dragStarted) {
                    m_activeDrag.operatorName = title;
                    m_activeDrag.paramName = p.name;
                    m_activeDrag.sourceLine = op->sourceLine;
                    m_activeDrag.originalValue[0] = result.startValue.x;
                    m_activeDrag.originalValue[1] = result.startValue.y;
                    m_activeDrag.originalValue[2] = 0;
                    m_activeDrag.originalValue[3] = 0;
                    m_activeDrag.active = true;
                }

                if (result.changed) {
                    float newValues[4] = {vec2Value.x, vec2Value.y, 0, 0};
                    op->setParam(p.name, newValues);
                }

                if (result.dragEnded && m_activeDrag.active &&
                    m_activeDrag.operatorName == title &&
                    m_activeDrag.paramName == p.name) {
                    float newValues[4] = {vec2Value.x, vec2Value.y, 0, 0};
                    if (m_paramChangeCallback) {
                        m_paramChangeCallback(m_activeDrag.operatorName, m_activeDrag.paramName,
                                              m_activeDrag.originalValue, newValues,
                                              m_activeDrag.sourceLine);
                    }
                    m_activeDrag.active = false;
                }

                gui.popId();
                gui.popId();
            }

            contentY += padHeight;
            continue;
        }

        // Vec3 params - use compact row
        if (p.type == ParamType::Vec3) {
            float y = contentY;
            float rowH = rowHeight * 2.0f;  // Match height calculation above
            float itemBottom = y + rowH;
            bool isVisible = (itemBottom > visibleTop) && (y < visibleBottom);

            if (isVisible) {
                gui.setCursorY(y);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                glm::vec3 vec3Value = {value[0], value[1], value[2]};
                auto result = gui.vec3Row(p.name.c_str(), &vec3Value, p.minVal, p.maxVal);

                if (result.dragStarted) {
                    m_activeDrag.operatorName = title;
                    m_activeDrag.paramName = p.name;
                    m_activeDrag.sourceLine = op->sourceLine;
                    m_activeDrag.originalValue[0] = result.startValue.x;
                    m_activeDrag.originalValue[1] = result.startValue.y;
                    m_activeDrag.originalValue[2] = result.startValue.z;
                    m_activeDrag.originalValue[3] = 0;
                    m_activeDrag.active = true;
                }

                if (result.changed) {
                    float newValues[4] = {vec3Value.x, vec3Value.y, vec3Value.z, 0};
                    op->setParam(p.name, newValues);
                }

                if (result.dragEnded && m_activeDrag.active &&
                    m_activeDrag.operatorName == title &&
                    m_activeDrag.paramName == p.name) {
                    float newValues[4] = {vec3Value.x, vec3Value.y, vec3Value.z, 0};
                    if (m_paramChangeCallback) {
                        m_paramChangeCallback(m_activeDrag.operatorName, m_activeDrag.paramName,
                                              m_activeDrag.originalValue, newValues,
                                              m_activeDrag.sourceLine);
                    }
                    m_activeDrag.active = false;
                }

                gui.popId();
                gui.popId();
            }

            contentY += rowH;
            continue;
        }

        // Multi-component params (Vec4 only now) - use Gui sliders with shared drag context
        gui.pushId(title.c_str());
        gui.pushId(p.name.c_str());

        for (int c = 0; c < componentCount; ++c) {
            float y = contentY;
            float itemBottom = y + rowHeight;
            bool isVisible = (itemBottom > visibleTop) && (y < visibleBottom);

            if (isVisible) {
                // Position Gui cursor and use pushId for component scope
                gui.setCursorY(y);
                gui.pushId(std::to_string(c).c_str());

                // Build component label (e.g., "position.X")
                std::string label = p.name + "." + componentLabels[c];

                // Use sliderEx to get drag state
                auto result = gui.sliderEx(label.c_str(), &value[c], p.minVal, p.maxVal);

                // On drag start: capture context for callback (store ALL component values)
                if (result.dragStarted) {
                    m_activeDrag.operatorName = title;
                    m_activeDrag.paramName = p.name;
                    m_activeDrag.sourceLine = op->sourceLine;
                    for (int i = 0; i < 4; ++i) {
                        m_activeDrag.originalValue[i] = value[i];
                    }
                    m_activeDrag.active = true;
                }

                // On value change: update operator with all current values
                if (result.changed) {
                    float newValues[4];
                    for (int i = 0; i < 4; ++i) newValues[i] = value[i];
                    newValues[c] = value[c];  // This is already updated by sliderEx
                    op->setParam(p.name, newValues);
                }

                // On drag end: fire callback with original and new values
                if (result.dragEnded && m_activeDrag.active &&
                    m_activeDrag.operatorName == title &&
                    m_activeDrag.paramName == p.name) {
                    float newValues[4];
                    for (int i = 0; i < 4; ++i) newValues[i] = value[i];
                    if (m_paramChangeCallback) {
                        m_paramChangeCallback(m_activeDrag.operatorName, m_activeDrag.paramName,
                                              m_activeDrag.originalValue, newValues,
                                              m_activeDrag.sourceLine);
                    }
                    m_activeDrag.active = false;
                }

                gui.popId();
            }

            contentY += rowHeight;
        }

        gui.popId();
        gui.popId();
    }

    // Cancel drag if mouse released outside (handled by Gui widgets now)
    if (mouseReleased && m_activeDrag.active) {
        m_activeDrag.active = false;
    }

    // End Gui content area
    gui.endArea();
    // Note: Layer clip rect is NOT cleared here - it persists until render() uses it.
    // It will be automatically cleared in begin() at the start of the next frame.
}

void ChainVisualizer::renderScreenInspector(const FrameInput& input, vivid::Context& ctx) {
    // Inspector panel renders on Panels layer (above nodes/thumbnails)
    m_overlay.setLayer(UILayer::Panels);

    // Font metrics
    const int labelFont = 0;
    float lineH = m_overlay.fontLineHeight(labelFont);
    float ascent = m_overlay.fontAscent(labelFont);
    if (lineH <= 0) lineH = 20.0f;
    if (ascent <= 0) ascent = 14.0f;

    // Scale layout values for HiDPI
    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;
    float inspectorWidth = m_inspectorWidth * scale;

    // Create scaled FrameInput for Gui widgets (HiDPI support)
    FrameInput scaledInput = input;
    scaledInput.mousePos *= scale;

    // Create Gui instance with inspector-compatible style
    Gui gui(m_overlay, scaledInput);
    gui.style().labelPosition = LabelPosition::Above;
    gui.style().valuePosition = ValuePosition::Right;
    gui.style().padding = 12.0f * scale;
    gui.style().widgetHeight = 20.0f * scale;
    gui.style().valueWidth = 60.0f * scale;
    gui.style().cornerRadius = 3.0f * scale;
    gui.style().widgetBackground = {0.2f, 0.2f, 0.25f, 1.0f};
    gui.style().sliderFill = {0.4f, 0.6f, 0.9f, 1.0f};
    gui.style().sliderFillActive = {0.5f, 0.7f, 1.0f, 1.0f};
    gui.style().textDim = {0.5f, 0.5f, 0.55f, 1.0f};

    // Layout
    const float padding = 12.0f * scale;
    const float rowHeight = lineH + 20.0f * scale + 4.0f * scale;
    const float headerHeight = lineH + padding * 2;

    // Single dropdown for display mode
    m_inspectorContentHeight = rowHeight + padding * 2;

    // Panel position (right side, below status bar)
    float statusBarHeight = lineH + 12.0f * scale;
    float panelX = input.width - inspectorWidth - padding;
    float panelY = statusBarHeight + padding;
    float panelHeight = headerHeight + m_inspectorContentHeight;

    // Store bounds for input blocking
    m_inspectorBounds.x = panelX;
    m_inspectorBounds.y = panelY;
    m_inspectorBounds.w = inspectorWidth;
    m_inspectorBounds.h = panelHeight;
    m_inspectorBounds.valid = true;

    // Colors
    glm::vec4 bgColor = {0.12f, 0.12f, 0.15f, 0.95f};
    glm::vec4 headerBg = {0.16f, 0.16f, 0.2f, 1.0f};
    glm::vec4 borderColor = {0.3f, 0.3f, 0.35f, 1.0f};
    glm::vec4 titleColor = {0.5f, 0.8f, 1.0f, 1.0f};

    // Draw panel background
    m_overlay.setLayer(UILayer::Panels - 1);
    float cornerRadius = 6.0f * scale;
    m_overlay.fillRoundedRect(panelX, panelY, inspectorWidth, panelHeight, cornerRadius, bgColor);
    m_overlay.strokeRoundedRect(panelX, panelY, inspectorWidth, panelHeight, cornerRadius, 1.0f * scale, borderColor);

    // Header
    m_overlay.fillRoundedRectTop(panelX, panelY, inspectorWidth, headerHeight, cornerRadius, headerBg);
    m_overlay.text("Screen", panelX + padding, panelY + padding + ascent, titleColor, labelFont);

    // Switch to main Panels layer for content
    m_overlay.setLayer(UILayer::Panels);

    // Content area
    float contentY = panelY + headerHeight + padding;
    float contentAreaX = panelX + padding;
    float contentAreaW = inspectorWidth - padding * 2;

    gui.beginArea(contentAreaX, panelY + headerHeight, contentAreaW, m_inspectorContentHeight);
    gui.setCursorY(contentY);

    // Display mode dropdown
    static const std::vector<std::string> displayModeLabels = {
        "Stretch",
        "Fit",
        "Fill",
        "Fill Horizontal",
        "Fill Vertical"
    };

    int currentMode = static_cast<int>(ctx.displayMode());
    gui.pushId("screen");
    if (gui.dropdown("Display Mode", &currentMode, displayModeLabels)) {
        ctx.displayMode(static_cast<DisplayMode>(currentMode));
    }
    gui.popId();

    gui.endArea();
}

void ChainVisualizer::renderDebugPanelOverlay(const FrameInput& input, vivid::Context& ctx) {
    const auto& debugValues = ctx.debugValues();
    if (debugValues.empty()) return;

    // Debug panel renders on Panels layer (above nodes/thumbnails)
    m_overlay.setLayer(UILayer::Panels);

    // Scale for HiDPI
    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;

    // Use font metrics for layout
    const int monoFont = 2;
    float lineH = m_overlay.fontLineHeight(monoFont);
    float ascent = m_overlay.fontAscent(monoFont);
    if (lineH <= 0) lineH = 20.0f * scale;  // Fallback (scaled)
    if (ascent <= 0) ascent = 14.0f * scale;

    const float padding = 8.0f * scale;
    const float lineHeight = lineH + 4 * scale;  // Add some spacing between rows
    const float nameWidth = 90.0f * scale;
    const float sparklineWidth = 100.0f * scale;
    const float sparklineHeight = lineH - 2 * scale;
    const float valueWidth = 65.0f * scale;
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
    m_overlay.fillRoundedRect(panelX, panelY, panelWidth, panelHeight, 4.0f * scale, bgColor);
    m_overlay.strokeRoundedRect(panelX, panelY, panelWidth, panelHeight, 4.0f * scale, 1.0f * scale, borderColor);

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
                m_overlay.line(x1, y1, x2, y2, 1.5f * scale, graphColor);
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
