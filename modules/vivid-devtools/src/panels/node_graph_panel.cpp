// Node Graph Panel Implementation
// Wraps NodeGraph to visualize operator chains
//
// Features:
// - Automatic node layout
// - Zoom/pan with mouse
// - Node selection
// - Double-click for solo mode
// - Texture thumbnail previews
// - Links for operator connections
// - Dashed links for value/trigger/event bindings

#include <vivid/devtools/panels/node_graph_panel.h>
#include <vivid/context.h>
#include <vivid/operator.h>
#include <vivid/effects/texture_operator.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/node_graph.h>
#include <vivid/gui/ui_style.h>
#include <vivid/viz_draw_list.h>
#include <webgpu/webgpu.h>
#include <iostream>

namespace vivid {

// Special node IDs for output nodes
static constexpr int SCREEN_NODE_ID = 9999;
static constexpr int SPEAKERS_NODE_ID = 9998;

struct NodeGraphPanel::Impl {
    // Callbacks
    NodeSelectCallback selectCallback;
    NodeDoubleClickCallback doubleClickCallback;

    // Node graph
    NodeGraph nodeGraph;
    bool nodeGraphInitialized = false;
    bool autoLayoutDone = false;
    size_t lastOperatorCount = 0;

    // Selection state
    int selectedNodeId = -1;
    std::string selectedOpName;

    // Focused node (3x preview)
    std::string focusedOperatorName;
    bool focusedModeActive = false;

    // Pending selection from external source
    std::string pendingEditorSelection;

    // Double-click handling
    int pendingDoubleClickNodeId = -1;

    // Context pointer for callbacks
    Context* ctx = nullptr;

    // CPU pixel scratch texture
    WGPUTexture cpuPixelScratchTex = nullptr;
    WGPUTextureView cpuPixelScratchView = nullptr;
    int cpuPixelScratchWidth = 0;
    int cpuPixelScratchHeight = 0;

    void releaseCpuPixelScratch() {
        if (cpuPixelScratchView) {
            wgpuTextureViewRelease(cpuPixelScratchView);
            cpuPixelScratchView = nullptr;
        }
        if (cpuPixelScratchTex) {
            wgpuTextureRelease(cpuPixelScratchTex);
            cpuPixelScratchTex = nullptr;
        }
        cpuPixelScratchWidth = 0;
        cpuPixelScratchHeight = 0;
    }

    WGPUTextureView uploadCpuPixelsToScratch(const Operator::CpuPixelView& view) {
        if (!view.valid() || !ctx) return nullptr;

        int width = view.width;
        int height = view.height;

        // Recreate texture if size changed
        if (width != cpuPixelScratchWidth || height != cpuPixelScratchHeight) {
            releaseCpuPixelScratch();

            WGPUTextureDescriptor desc{};
            desc.label.data = "cpu_pixel_scratch";
            desc.label.length = WGPU_STRLEN;
            desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            desc.dimension = WGPUTextureDimension_2D;
            desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
            desc.format = WGPUTextureFormat_BGRA8Unorm;
            desc.mipLevelCount = 1;
            desc.sampleCount = 1;

            cpuPixelScratchTex = wgpuDeviceCreateTexture(ctx->device(), &desc);
            if (!cpuPixelScratchTex) return nullptr;

            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.baseMipLevel = 0;
            viewDesc.mipLevelCount = 1;
            viewDesc.baseArrayLayer = 0;
            viewDesc.arrayLayerCount = 1;
            viewDesc.aspect = WGPUTextureAspect_All;

            cpuPixelScratchView = wgpuTextureCreateView(cpuPixelScratchTex, &viewDesc);
            cpuPixelScratchWidth = width;
            cpuPixelScratchHeight = height;
        }

        // Upload pixels
        WGPUTexelCopyTextureInfo dstInfo{};
        dstInfo.texture = cpuPixelScratchTex;
        dstInfo.mipLevel = 0;
        dstInfo.origin = {0, 0, 0};
        dstInfo.aspect = WGPUTextureAspect_All;

        size_t rowStride = view.rowStride();
        WGPUTexelCopyBufferLayout layout{};
        layout.offset = 0;
        layout.bytesPerRow = static_cast<uint32_t>(rowStride);
        layout.rowsPerImage = static_cast<uint32_t>(height);

        WGPUExtent3D writeSize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        size_t dataSize = rowStride * height;

        wgpuQueueWriteTexture(ctx->queue(), &dstInfo, view.data, dataSize, &layout, &writeSize);

        return cpuPixelScratchView;
    }
};

NodeGraphPanel::NodeGraphPanel() {
    m_config.id = "nodegraph";
    m_config.title = "";  // No title bar for fill panel
    m_config.bounds = {0, 0, 800, 600};
    m_config.dockSide = DockSide::Fill;
    m_config.visible = false;
    m_config.resizable = false;
    m_config.draggable = false;
    m_config.minWidth = 200.0f;
    m_config.minHeight = 200.0f;
}

NodeGraphPanel::~NodeGraphPanel() {
    if (m_impl) {
        m_impl->releaseCpuPixelScratch();
    }
}

bool NodeGraphPanel::init(Context& ctx, WGPUTextureFormat surfaceFormat) {
    m_impl = std::make_unique<Impl>();
    m_impl->ctx = &ctx;

    // Set up callbacks
    m_impl->nodeGraph.setDoubleClickCallback([this](int nodeId) {
        m_impl->pendingDoubleClickNodeId = nodeId;
    });

    return true;
}

void NodeGraphPanel::shutdown() {
    if (m_impl) {
        m_impl->releaseCpuPixelScratch();
    }
    m_impl.reset();
}

void NodeGraphPanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                            const FrameInput& input, float scale) {
    if (!m_config.visible || !m_impl || !m_impl->ctx) return;

    Context& ctx = *m_impl->ctx;
    const auto& operators = ctx.registeredOperators();
    if (operators.empty()) return;

    // Build input for node graph
    glm::vec2 scaledMousePos = input.mousePos * scale;

    NodeGraphInput graphInput;
    graphInput.mousePos = scaledMousePos;
    static glm::vec2 lastMousePos = scaledMousePos;
    graphInput.mouseDelta = scaledMousePos - lastMousePos;
    lastMousePos = scaledMousePos;
    graphInput.scroll = input.scroll;
    graphInput.mouseDown[0] = input.mouseDown[0];
    graphInput.mouseDown[1] = input.mouseDown[1];
    graphInput.mouseDown[2] = input.mouseDown[2];

    static bool lastMouseDown[3] = {false, false, false};
    for (int i = 0; i < 3; i++) {
        graphInput.mouseClicked[i] = input.mouseDown[i] && !lastMouseDown[i];
        graphInput.mouseReleased[i] = !input.mouseDown[i] && lastMouseDown[i];
        lastMouseDown[i] = input.mouseDown[i];
    }
    graphInput.keyCtrl = input.keyCtrl;
    graphInput.keyShift = input.keyShift;
    graphInput.keyAlt = input.keyAlt;
    graphInput.keyF = input.isKeyPressed(Key::F);
    graphInput.key1 = input.isKeyPressed(Key::Num1);
    graphInput.keyUp = input.isKeyPressed(Key::Up);
    graphInput.keyDown = input.isKeyPressed(Key::Down);
    graphInput.keyLeft = input.isKeyPressed(Key::Left);
    graphInput.keyRight = input.isKeyPressed(Key::Right);
    graphInput.keyEnter = input.isKeyPressed(Key::Enter);
    graphInput.keyB = input.isKeyPressed(Key::B);
    graphInput.keyEscape = input.isKeyPressed(Key::Escape);
    graphInput.time = input.time;

    // Begin node graph editor
    m_impl->nodeGraph.beginEditor(canvas, bounds.z, bounds.w, graphInput);

    // Add nodes for each operator
    for (size_t i = 0; i < operators.size(); ++i) {
        const OperatorInfo& info = operators[i];
        if (!info.op) continue;

        int nodeId = static_cast<int>(i);

        m_impl->nodeGraph.beginNode(nodeId);
        m_impl->nodeGraph.setNodeTitle(info.name);

        // Set content callback for thumbnail rendering
        Operator* op = info.op;
        Impl* impl = m_impl.get();
        m_impl->nodeGraph.setNodeContent([op, impl](OverlayCanvas& canvas, float x, float y, float w, float h) {
            if (!op) return;

            OutputKind kind = op->outputKind();

            // Try operator's custom visualization first
            VizDrawList dl(canvas);
            if (op->drawVisualization(&dl, x, y, x + w, y + h)) {
                return;
            }

            // Fallback based on output type
            if (kind == OutputKind::Texture) {
                WGPUTextureView view = op->outputView();
                if (view) {
                    float srcAspect = 16.0f / 9.0f;
                    if (auto* texOp = dynamic_cast<effects::TextureOperator*>(op)) {
                        int texW = texOp->outputWidth();
                        int texH = texOp->outputHeight();
                        if (texW > 0 && texH > 0) {
                            srcAspect = static_cast<float>(texW) / static_cast<float>(texH);
                        }
                    }

                    float areaAspect = w / h;
                    float drawW, drawH, drawX, drawY;
                    if (srcAspect > areaAspect) {
                        drawW = w;
                        drawH = w / srcAspect;
                        drawX = x;
                        drawY = y + (h - drawH) * 0.5f;
                    } else {
                        drawH = h;
                        drawW = h * srcAspect;
                        drawX = x + (w - drawW) * 0.5f;
                        drawY = y;
                    }
                    canvas.texturedRect(drawX, drawY, drawW, drawH, view);
                } else {
                    canvas.fillRect(x, y, w, h, {0.15f, 0.15f, 0.2f, 1.0f});
                }
            } else if (kind == OutputKind::Geometry) {
                canvas.fillRect(x, y, w, h, {0.12f, 0.2f, 0.28f, 1.0f});
                float cx = x + w * 0.5f;
                float cy = y + h * 0.5f;
                float sz = std::min(w, h) * 0.3f;
                glm::vec4 lineColor = {0.4f, 0.7f, 1.0f, 0.8f};
                canvas.strokeRect(cx - sz, cy - sz * 0.6f, sz * 1.6f, sz * 1.2f, 1.5f, lineColor);
            } else if (kind == OutputKind::CpuPixels) {
                auto cpuView = op->cpuPixelView();
                WGPUTextureView view = nullptr;
                if (cpuView.valid() && impl) {
                    view = impl->uploadCpuPixelsToScratch(cpuView);
                }
                if (view) {
                    float srcAspect = static_cast<float>(cpuView.width) / static_cast<float>(cpuView.height);
                    float areaAspect = w / h;
                    float drawW, drawH, drawX, drawY;
                    if (srcAspect > areaAspect) {
                        drawW = w;
                        drawH = w / srcAspect;
                        drawX = x;
                        drawY = y + (h - drawH) * 0.5f;
                    } else {
                        drawH = h;
                        drawW = h * srcAspect;
                        drawX = x + (w - drawW) * 0.5f;
                        drawY = y;
                    }
                    canvas.texturedRect(drawX, drawY, drawW, drawH, view);
                } else {
                    canvas.fillRect(x, y, w, h, {0.15f, 0.18f, 0.12f, 1.0f});
                }
            } else if (kind == OutputKind::Audio) {
                canvas.fillRect(x, y, w, h, {0.2f, 0.12f, 0.25f, 1.0f});
                float centerY = y + h * 0.5f;
                glm::vec4 waveColor = {0.7f, 0.5f, 0.9f, 0.9f};
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
                canvas.fillRect(x, y, w, h, {0.15f, 0.15f, 0.18f, 1.0f});
            }
        });

        // Add input pins
        size_t numInputs = info.op->inputCount();
        for (size_t j = 0; j < numInputs; ++j) {
            if (info.op->getInput(static_cast<int>(j))) {
                int pinId = nodeId * 100 + static_cast<int>(j) + 1;
                m_impl->nodeGraph.beginInputAttribute(pinId);
                std::string label = info.op->getInputName(static_cast<int>(j));
                if (label.empty()) {
                    label = "in" + std::to_string(j);
                }
                m_impl->nodeGraph.pinLabel(label);
                m_impl->nodeGraph.endInputAttribute();
            }
        }

        // Check for value bindings
        auto paramDecls = info.op->params();
        bool hasValueBindings = false;
        for (const auto& p : paramDecls) {
            if (p.boundOperator) {
                hasValueBindings = true;
                break;
            }
        }
        if (hasValueBindings) {
            int valuePinId = nodeId * 100 + 50;
            m_impl->nodeGraph.beginInputAttribute(valuePinId);
            m_impl->nodeGraph.pinLabel("values");
            m_impl->nodeGraph.endInputAttribute();
        }

        // Check for trigger source
        if (info.op->triggerSource()) {
            int trigPinId = nodeId * 100 + 51;
            m_impl->nodeGraph.beginInputAttribute(trigPinId);
            m_impl->nodeGraph.pinLabel("trig");
            m_impl->nodeGraph.endInputAttribute();
        }

        // Check for event source
        if (info.op->eventSource()) {
            int evtPinId = nodeId * 100 + 52;
            m_impl->nodeGraph.beginInputAttribute(evtPinId);
            m_impl->nodeGraph.pinLabel("evt");
            m_impl->nodeGraph.endInputAttribute();
        }

        // Output pin
        int outputPinId = nodeId * 100;
        m_impl->nodeGraph.beginOutputAttribute(outputPinId);
        m_impl->nodeGraph.pinLabel("out");
        m_impl->nodeGraph.endOutputAttribute();

        m_impl->nodeGraph.endNode();
    }

    // Add Screen output node
    Operator* outputOp = ctx.hasChain() ? ctx.chain().getOutput() : nullptr;
    int outputNodeId = -1;
    if (outputOp) {
        for (size_t i = 0; i < operators.size(); ++i) {
            if (operators[i].op == outputOp) {
                outputNodeId = static_cast<int>(i);
                break;
            }
        }

        if (outputNodeId >= 0) {
            m_impl->nodeGraph.beginNode(SCREEN_NODE_ID);
            m_impl->nodeGraph.setNodeTitle("Screen");
            m_impl->nodeGraph.beginInputAttribute(SCREEN_NODE_ID * 100 + 1);
            m_impl->nodeGraph.pinLabel("display");
            m_impl->nodeGraph.endInputAttribute();
            m_impl->nodeGraph.endNode();
        }
    }

    // Add Speakers output node
    Operator* audioOutputOp = ctx.hasChain() ? ctx.chain().getAudioOutput() : nullptr;
    int audioOutputNodeId = -1;
    if (audioOutputOp) {
        for (size_t i = 0; i < operators.size(); ++i) {
            if (operators[i].op == audioOutputOp) {
                audioOutputNodeId = static_cast<int>(i);
                break;
            }
        }

        if (audioOutputNodeId >= 0) {
            m_impl->nodeGraph.beginNode(SPEAKERS_NODE_ID);
            m_impl->nodeGraph.setNodeTitle("Speakers");
            m_impl->nodeGraph.beginInputAttribute(SPEAKERS_NODE_ID * 100 + 1);
            m_impl->nodeGraph.pinLabel("audio");
            m_impl->nodeGraph.endInputAttribute();
            m_impl->nodeGraph.endNode();
        }
    }

    // Add links based on operator connections
    int linkId = 0;
    for (size_t i = 0; i < operators.size(); ++i) {
        const OperatorInfo& info = operators[i];
        if (!info.op) continue;

        int nodeId = static_cast<int>(i);
        size_t numInputs = info.op->inputCount();

        for (size_t j = 0; j < numInputs; ++j) {
            Operator* inputOp = info.op->getInput(static_cast<int>(j));
            if (!inputOp) continue;

            for (size_t k = 0; k < operators.size(); ++k) {
                if (operators[k].op == inputOp) {
                    int srcNodeId = static_cast<int>(k);
                    int srcOutputPinId = srcNodeId * 100;
                    int dstInputPinId = nodeId * 100 + static_cast<int>(j) + 1;
                    m_impl->nodeGraph.link(linkId++, srcOutputPinId, dstInputPinId);
                    break;
                }
            }
        }
    }

    // Link to Screen node
    if (outputNodeId >= 0) {
        m_impl->nodeGraph.link(linkId++, outputNodeId * 100, SCREEN_NODE_ID * 100 + 1);
    }

    // Link to Speakers node
    if (audioOutputNodeId >= 0) {
        m_impl->nodeGraph.link(linkId++, audioOutputNodeId * 100, SPEAKERS_NODE_ID * 100 + 1);
    }

    // Dashed links for value bindings
    glm::vec4 valueBindingColor = {1.0f, 0.7f, 0.3f, 0.9f};
    for (size_t i = 0; i < operators.size(); ++i) {
        const OperatorInfo& info = operators[i];
        if (!info.op) continue;

        int dstNodeId = static_cast<int>(i);
        auto paramDecls = info.op->params();
        for (const auto& param : paramDecls) {
            if (!param.boundOperator) continue;

            for (size_t k = 0; k < operators.size(); ++k) {
                if (operators[k].op == param.boundOperator) {
                    int srcNodeId = static_cast<int>(k);
                    int srcOutputPinId = srcNodeId * 100;
                    int dstInputPinId = dstNodeId * 100 + 50;
                    m_impl->nodeGraph.linkDashed(linkId++, srcOutputPinId, dstInputPinId, valueBindingColor);
                    break;
                }
            }
        }
    }

    // Dashed links for triggers
    glm::vec4 triggerColor = {0.4f, 0.8f, 1.0f, 0.9f};
    for (size_t i = 0; i < operators.size(); ++i) {
        const OperatorInfo& info = operators[i];
        if (!info.op) continue;

        Operator* triggerSrc = info.op->triggerSource();
        if (!triggerSrc) continue;

        int dstNodeId = static_cast<int>(i);
        for (size_t k = 0; k < operators.size(); ++k) {
            if (operators[k].op == triggerSrc) {
                int srcNodeId = static_cast<int>(k);
                int srcOutputPinId = srcNodeId * 100;
                int dstInputPinId = dstNodeId * 100 + 51;
                m_impl->nodeGraph.linkDashed(linkId++, srcOutputPinId, dstInputPinId, triggerColor);
                break;
            }
        }
    }

    // Dashed links for events
    glm::vec4 eventColor = {0.4f, 1.0f, 0.6f, 0.9f};
    for (size_t i = 0; i < operators.size(); ++i) {
        const OperatorInfo& info = operators[i];
        if (!info.op) continue;

        Operator* eventSrc = info.op->eventSource();
        if (!eventSrc) continue;

        int dstNodeId = static_cast<int>(i);
        for (size_t k = 0; k < operators.size(); ++k) {
            if (operators[k].op == eventSrc) {
                int srcNodeId = static_cast<int>(k);
                int srcOutputPinId = srcNodeId * 100;
                int dstInputPinId = dstNodeId * 100 + 52;
                m_impl->nodeGraph.linkDashed(linkId++, srcOutputPinId, dstInputPinId, eventColor);
                break;
            }
        }
    }

    // Auto-layout on first render or when operator count changes
    if (operators.size() != m_impl->lastOperatorCount) {
        m_impl->autoLayoutDone = false;
        m_impl->lastOperatorCount = operators.size();
    }
    if (!m_impl->autoLayoutDone && !operators.empty()) {
        m_impl->nodeGraph.autoLayout();
        m_impl->nodeGraph.zoomToFit();
        m_impl->autoLayoutDone = true;
    }

    // Apply pending editor selection
    if (!m_impl->pendingEditorSelection.empty()) {
        for (size_t i = 0; i < operators.size(); ++i) {
            if (operators[i].name == m_impl->pendingEditorSelection) {
                m_impl->nodeGraph.selectNode(static_cast<int>(i));
                break;
            }
        }
        m_impl->pendingEditorSelection.clear();
    }

    // End node graph editor
    m_impl->nodeGraph.endEditor();

    // Sync selection state
    int selectedNodeId = m_impl->nodeGraph.getSelectedNode();
    if (selectedNodeId >= 0 && selectedNodeId != SCREEN_NODE_ID && selectedNodeId != SPEAKERS_NODE_ID) {
        if (static_cast<size_t>(selectedNodeId) < operators.size()) {
            m_impl->selectedOpName = operators[selectedNodeId].name;
            m_impl->selectedNodeId = selectedNodeId;

            if (m_impl->selectCallback) {
                m_impl->selectCallback(m_impl->selectedOpName);
            }
        }
    } else if (selectedNodeId < 0) {
        m_impl->selectedOpName.clear();
        m_impl->selectedNodeId = -1;
    }

    // Handle double-click
    if (m_impl->pendingDoubleClickNodeId >= 0) {
        int nodeId = m_impl->pendingDoubleClickNodeId;
        m_impl->pendingDoubleClickNodeId = -1;

        if (nodeId != SCREEN_NODE_ID && nodeId != SPEAKERS_NODE_ID) {
            if (static_cast<size_t>(nodeId) < operators.size()) {
                const OperatorInfo& info = operators[nodeId];
                if (info.op && m_impl->doubleClickCallback) {
                    m_impl->doubleClickCallback(info.name);
                }
            }
        }
    }
}

bool NodeGraphPanel::handleInput(const FrameInput& input) {
    if (!m_impl) return false;
    return m_impl->nodeGraph.consumedInput();
}

void NodeGraphPanel::selectNode(const std::string& name) {
    if (m_impl) {
        m_impl->pendingEditorSelection = name;
    }
}

void NodeGraphPanel::setFocusedNode(const std::string& name) {
    if (m_impl) {
        m_impl->focusedOperatorName = name;
        m_impl->focusedModeActive = !name.empty();
    }
}

void NodeGraphPanel::clearFocusedNode() {
    if (m_impl) {
        m_impl->focusedOperatorName.clear();
        m_impl->focusedModeActive = false;
    }
}

void NodeGraphPanel::onNodeSelect(NodeSelectCallback callback) {
    if (m_impl) {
        m_impl->selectCallback = std::move(callback);
    }
}

void NodeGraphPanel::onNodeDoubleClick(NodeDoubleClickCallback callback) {
    if (m_impl) {
        m_impl->doubleClickCallback = std::move(callback);
    }
}

} // namespace vivid
