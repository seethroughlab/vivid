// DevTools main orchestrator implementation
//
// This is the unified entry point for all devtools functionality:
// - NodeGraph panel for operator visualization
// - Inspector panel for parameter editing
// - Performance panel for real-time metrics
// - StatusBar panel for record/snapshot controls

#include <vivid/devtools/devtools.h>
#include <vivid/devtools/preferences.h>
#include <vivid/gui/input_state.h>
#include <vivid/devtools/panels/node_graph_panel.h>
#include <vivid/devtools/panels/inspector_panel.h>
#include <vivid/devtools/panels/status_bar_panel.h>
#include <vivid/devtools/panels/performance_panel.h>
#include <vivid/devtools/panels/console_panel.h>
#include <vivid/devtools/panels/preset_panel.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <vivid/snapshot.h>
#include <vivid/midi_map.h>
#include <vivid/log.h>
#include <vivid/asset_loader.h>
#include "effects/font_atlas.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <cstdio>
#include <iostream>

namespace vivid {

// Helper to convert FrameInput to gui::InputState
// This allows DevTools to accept FrameInput (for backward compatibility)
// while using the decoupled gui::InputState internally.
static gui::InputState toInputState(const FrameInput& input) {
    gui::InputState state;
    state.width = input.width;
    state.height = input.height;
    state.contentScale = input.contentScale;
    state.dt = input.dt;
    state.time = input.time;
    state.mousePos = input.mousePos;
    state.mouseDelta = input.mouseDelta;
    state.scroll = input.scroll;
    std::memcpy(state.mouseDown, input.mouseDown, sizeof(state.mouseDown));
    std::memcpy(state.mouseClicked, input.mouseClicked, sizeof(state.mouseClicked));
    std::memcpy(state.mouseReleased, input.mouseReleased, sizeof(state.mouseReleased));
    state.keyCtrl = input.keyCtrl;
    state.keyShift = input.keyShift;
    state.keyAlt = input.keyAlt;
    state.keySuper = input.keySuper;
    std::memcpy(state.keyPressed, input.keyPressed, sizeof(state.keyPressed));
    std::memcpy(state.keyDown, input.keyDown, sizeof(state.keyDown));
    state.surfaceFormat = input.surfaceFormat;
    return state;
}

DevTools& DevTools::instance() {
    static DevTools s_instance;
    return s_instance;
}

DevTools::~DevTools() {
    shutdown();
}

bool DevTools::init(Context& ctx, WGPUTextureFormat surfaceFormat) {
    if (m_initialized) return true;

    m_ctx = &ctx;

    // Load user preferences (includes theme/style)
    Preferences::instance().load();
    m_style = Preferences::instance().style();

    // Set up callback for when preferences change
    Preferences::instance().onStyleChange([this](const UIStyle& newStyle) {
        m_style = newStyle;
        // Note: scale will be updated in render() from input.contentScale
    });

    // Initialize canvas
    m_canvas = std::make_unique<OverlayCanvas>();
    if (!m_canvas->init(ctx.device(), ctx.queue(), surfaceFormat)) {
        std::cerr << "[vivid-devtools] Failed to initialize canvas\n";
        return false;
    }

    // Load fonts for text rendering - use JetBrains Mono for everything
    auto& assets = AssetLoader::instance();
    std::string fontPath = assets.resolve("devtools/fonts/JetBrainsMono-Regular.ttf").string();

    float scale = ctx.contentScale();
    if (scale < 1.0f) scale = 1.0f;

    // Set style scale (may be 1.0 during init if window not ready; updated in render())
    m_style.scale = scale;

    // Load JetBrains Mono at different sizes for UI hierarchy
    // Fonts are loaded at PHYSICAL pixel size for crisp HiDPI rendering
    // Use textHiDPI() methods which compensate for content scale
    // Index 0: Labels (14px logical = 28px physical on 2x display)
    m_fonts[0] = std::make_unique<FontAtlas>();
    if (m_fonts[0]->load(ctx, fontPath, 14.0f * scale)) {
        m_canvas->setFont(0, m_fonts[0].get());
        std::cerr << "[vivid-devtools] Loaded JetBrains Mono " << (14.0f * scale) << "px\n";
    }

    // Index 1: Titles (16px logical = 32px physical on 2x display)
    m_fonts[1] = std::make_unique<FontAtlas>();
    if (m_fonts[1]->load(ctx, fontPath, 16.0f * scale)) {
        m_canvas->setFont(1, m_fonts[1].get());
        std::cerr << "[vivid-devtools] Loaded JetBrains Mono " << (16.0f * scale) << "px\n";
    }

    // Enable HiDPI text mode since fonts are loaded at physical pixel size
    // This makes text(), measureText(), etc. automatically compensate for contentScale
    m_canvas->setHiDPITextMode(true);

    // Initialize panel manager
    m_panelManager = std::make_unique<PanelManager>();

    // Create and add panels
    // Order matters: panels are rendered back-to-front

    // StatusBar (top edge) - render first (background)
    auto statusBar = std::make_unique<StatusBarPanel>();
    statusBar->setVideoExporter(&m_exporter);
    statusBar->onSnapshot([this]() {
        m_snapshotRequested = true;
    });
    statusBar->onRecord([this](bool start, ExportCodec codec) {
        if (start && m_ctx) {
            // Generate output path in the project directory
            std::string projectDir = ".";
            const std::string& chainPath = m_ctx->chainPath();
            if (!chainPath.empty()) {
                size_t lastSlash = chainPath.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                    projectDir = chainPath.substr(0, lastSlash);
            }
            std::string outputPath = VideoExporter::generateOutputPath(projectDir, codec);

            int width = m_ctx->width();
            int height = m_ctx->height();
            WGPUTexture outputTex = m_ctx->chain().outputTexture();
            if (outputTex) {
                width = static_cast<int>(wgpuTextureGetWidth(outputTex));
                height = static_cast<int>(wgpuTextureGetHeight(outputTex));
            }

            float fps = 60.0f;
            bool hasAudio = m_ctx->chain().getAudioOutput() != nullptr;
            bool started = false;
            if (hasAudio) {
                started = m_exporter.startWithAudio(outputPath, width, height, fps, codec, 48000, 2);
                if (started) {
                    m_ctx->chain().startAudioRecordingTap();
                    m_ctx->setRecordingMode(true, fps);
                }
            } else {
                started = m_exporter.start(outputPath, width, height, fps, codec);
                if (started) {
                    m_ctx->setRecordingMode(true, fps);
                }
            }

            if (!started) {
                std::cerr << "[vivid-devtools] Failed to start recording: " << m_exporter.error() << "\n";
            }
        } else if (!start && m_ctx) {
            m_ctx->chain().stopAudioRecordingTap();
            m_exporter.stop();
            m_ctx->setRecordingMode(false);
        }
    });
    statusBar->onGridOpacityChange([this](float opacity) {
        m_gridOpacity = opacity;
        Preferences::instance().setGridOpacity(opacity);
    });
    m_panelManager->addPanel(std::move(statusBar));

    // NodeGraph (fill) - background for visualizer
    auto nodeGraph = std::make_unique<NodeGraphPanel>();
    nodeGraph->onNodeSelect([this](const std::string& name) {
        if (name.empty()) {
            // Node deselected — auto-hide inspector
            hidePanel("inspector");
            return;
        }
        // Handle virtual nodes (Screen, Speakers)
        if (name == "__screen__") {
            auto* inspector = m_panelManager->getPanelAs<InspectorPanel>("inspector");
            if (inspector && m_ctx) {
                inspector->setScreenMode(m_ctx);
                showPanel("inspector");
            }
            return;
        }
        if (name == "__speakers__") {
            if (m_ctx && m_ctx->hasChain()) {
                Operator* audioOut = m_ctx->chain().getAudioOutput();
                if (audioOut) {
                    auto* inspector = m_panelManager->getPanelAs<InspectorPanel>("inspector");
                    if (inspector) {
                        inspector->setSelectedOperator(audioOut, "Speakers");
                        showPanel("inspector");
                    }
                }
            }
            return;
        }
        // When a node is selected, update the inspector and show it
        if (m_ctx) {
            const auto& operators = m_ctx->registeredOperators();
            for (const auto& info : operators) {
                if (info.name == name) {
                    auto* inspector = m_panelManager->getPanelAs<InspectorPanel>("inspector");
                    if (inspector) {
                        inspector->setSelectedOperator(info.op, name);
                        showPanel("inspector");
                    }
                    break;
                }
            }
        }
    });
    nodeGraph->onNodeDoubleClick([this](const std::string& name) {
        // Double-click enters solo mode
        if (m_ctx) {
            const auto& operators = m_ctx->registeredOperators();
            for (const auto& info : operators) {
                if (info.name == name) {
                    enterSoloMode(info.op, name);
                    break;
                }
            }
        }
    });
    m_panelManager->addPanel(std::move(nodeGraph));

    // Inspector (floating)
    auto inspector = std::make_unique<InspectorPanel>();
    inspector->onParamChange([this](const std::string& opName, const std::string& paramName,
                                     const float oldVal[4], const float newVal[4], int sourceLine) {
        // Forward to external callback
        if (m_paramChangeCallback) {
            m_paramChangeCallback(opName, paramName, oldVal, newVal, sourceLine);
        }
    });
    m_panelManager->addPanel(std::move(inspector));

    // Performance (floating - for real-time metrics)
    auto performance = std::make_unique<PerformancePanel>();
    m_panelManager->addPanel(std::move(performance));

    // Console (floating - read-only log overlay)
    auto console = std::make_unique<ConsolePanel>();
    auto* consolePtr = console.get();
    m_panelManager->addPanel(std::move(console));

    // Presets (floating - snapshot management)
    auto presets = std::make_unique<PresetPanel>();
    m_panelManager->addPanel(std::move(presets));

    // Set Log callback to feed messages into the console panel
    Log::setCallback([consolePtr](LogLevel level, const char* file, int line, const std::string& message) {
        consolePtr->pushMessage(level, file, line, message);
    });

    // Initialize panel manager (initializes all panels)
    if (!m_panelManager->init(ctx, surfaceFormat)) {
        std::cerr << "[vivid-devtools] Failed to initialize panel manager\n";
        return false;
    }

    // Register default keyboard shortcuts
    registerDefaultShortcuts();

    // Create preferences panel
    m_preferencesPanel = std::make_unique<PreferencesPanel>();
    m_preferencesPanel->setStyle(&m_style);
    m_preferencesPanel->setShortcuts(&m_shortcuts);

    // Load grid opacity from preferences
    m_gridOpacity = Preferences::instance().gridOpacity();

    // Restore saved panel bounds for floating panels
    const char* floatingPanelIds[] = {"inspector", "performance", "console", "presets"};
    for (const char* id : floatingPanelIds) {
        glm::vec4 savedBounds;
        if (Preferences::instance().getPanelBounds(id, savedBounds)) {
            if (Panel* p = m_panelManager->getPanel(id)) {
                p->setBounds(savedBounds);
            }
        }
    }

    // Set initial panel visibility:
    // NodeGraph — always visible (background)
    // StatusBar — always visible
    // Inspector — hidden (auto-shows on node selection)
    // Performance — hidden (toggle with Cmd+1)
    // Console — hidden (toggle with Cmd+2)
    showPanel("nodegraph");
    showPanel("statusbar");
    hidePanel("inspector");
    hidePanel("performance");
    hidePanel("console");
    hidePanel("presets");

    m_initialized = true;
    std::cerr << "[vivid-devtools] Initialized with " << m_panelManager->panelCount() << " panels\n";
    return true;
}

void DevTools::registerDefaultShortcuts() {
    // Cmd/Ctrl+1: Toggle Performance Panel
    m_shortcuts.registerShortcut({
        GLFW_KEY_1,
        ShortcutManager::MOD_CMD_OR_CTRL,
        "toggle_performance",
        "Toggle Performance",
        [this]() { togglePanel("performance"); }
    });

    // Cmd/Ctrl+2: Toggle Console Panel
    m_shortcuts.registerShortcut({
        GLFW_KEY_2,
        ShortcutManager::MOD_CMD_OR_CTRL,
        "toggle_console",
        "Toggle Console",
        [this]() { togglePanel("console"); }
    });

    // F1: Show Help
    m_shortcuts.registerShortcut({
        GLFW_KEY_F1,
        0,
        "show_help",
        "Show Shortcuts",
        [this]() {
            if (m_helpCallback) {
                m_helpCallback();
            }
        }
    });

    // Cmd/Ctrl+,: Open Preferences
    m_shortcuts.registerShortcut({
        GLFW_KEY_COMMA,
        ShortcutManager::MOD_CMD_OR_CTRL,
        "open_preferences",
        "Open Preferences",
        [this]() { showPreferences(); }
    });

    // Escape: Exit solo mode if active
    m_shortcuts.registerShortcut({
        GLFW_KEY_ESCAPE,
        0,
        "exit_solo",
        "Exit Solo Mode",
        [this]() {
            if (m_inSoloMode) {
                exitSoloMode();
            }
        }
    });

    // Cmd/Ctrl+3: Toggle Presets Panel
    m_shortcuts.registerShortcut({
        GLFW_KEY_3,
        ShortcutManager::MOD_CMD_OR_CTRL,
        "toggle_presets",
        "Toggle Presets",
        [this]() { togglePanel("presets"); }
    });

    // Cmd/Ctrl+G: Toggle Background Grid
    m_shortcuts.registerShortcut({
        GLFW_KEY_G,
        ShortcutManager::MOD_CMD_OR_CTRL,
        "toggle_grid",
        "Toggle Background Grid",
        [this]() { toggleBackgroundGrid(); }
    });

    // Number keys 1-9 (no modifier): recall snapshot by position
    for (int i = 0; i < 9; i++) {
        int key = GLFW_KEY_1 + i;
        int idx = i;
        std::string id = "recall_snapshot_" + std::to_string(i + 1);
        std::string label = "Recall Snapshot " + std::to_string(i + 1);
        m_shortcuts.registerShortcut({
            key,
            0,  // no modifier
            id,
            label,
            [this, idx]() {
                auto* panel = m_panelManager->getPanelAs<PresetPanel>("presets");
                if (!panel || !m_ctx) return;
                Chain& chain = m_ctx->chain();
                auto& store = chain.snapshots();
                if (idx < store.size()) {
                    store.recall(idx, chain, panel->crossfadeDuration());
                }
            }
        });
    }

    // Shift+1-9: save/overwrite snapshot at position
    for (int i = 0; i < 9; i++) {
        int key = GLFW_KEY_1 + i;
        int idx = i;
        std::string id = "save_snapshot_" + std::to_string(i + 1);
        std::string label = "Save Snapshot " + std::to_string(i + 1);
        m_shortcuts.registerShortcut({
            key,
            ShortcutManager::MOD_SHIFT,
            id,
            label,
            [this, idx]() {
                if (!m_ctx) return;
                Chain& chain = m_ctx->chain();
                auto& store = chain.snapshots();
                char nameBuf[32];
                snprintf(nameBuf, sizeof(nameBuf), "Snapshot %d", idx + 1);
                if (idx < store.size()) {
                    // Overwrite existing
                    store.remove(idx);
                }
                // Insert at position (capture appends, then move)
                int newIdx = store.capture(nameBuf, chain);
                if (newIdx != idx && idx < store.size()) {
                    store.move(newIdx, idx);
                }
                // Auto-save
                if (!m_ctx->chainPath().empty()) {
                    std::filesystem::path chainFile(m_ctx->chainPath());
                    std::string savePath = (chainFile.parent_path() / "vivid-snapshots.json").string();
                    store.save(savePath);
                }
            }
        });
    }
}

void DevTools::shutdown() {
    if (!m_initialized) return;

    // Save floating panel bounds before shutdown
    if (m_panelManager) {
        const char* floatingPanelIds[] = {"inspector", "performance", "console", "presets"};
        for (const char* id : floatingPanelIds) {
            if (Panel* p = m_panelManager->getPanel(id)) {
                Preferences::instance().setPanelBounds(id, p->bounds());
            }
        }
        Preferences::instance().save();
    }

    // Clear Log callback before destroying panels (ConsolePanel may be the target)
    Log::clearCallback();

    if (m_panelManager) {
        m_panelManager->shutdown();
        m_panelManager.reset();
    }

    m_canvas->cleanup();
    m_canvas.reset();

    m_initialized = false;
    std::cerr << "[vivid-devtools] Shutdown\n";
}

void DevTools::update() {
    if (!m_initialized) return;
    m_panelManager->update();

    if (auto* statusBar = m_panelManager->getPanelAs<StatusBarPanel>("statusbar")) {

        // Sync pending change count
        statusBar->setPendingChangeCount(m_pendingChangeCount);
        statusBar->setMcpWarning(m_mcpWarning);

        // Sync grid opacity slider
        statusBar->setGridOpacity(m_gridOpacity);
    }
}

void DevTools::render(WGPURenderPassEncoder pass, const FrameInput& input, Context& ctx) {
    if (!m_initialized) return;

    // Convert FrameInput to gui::InputState for GUI module
    gui::InputState guiInput = toInputState(input);

    // Begin canvas - handles HiDPI scaling internally
    m_canvas->begin(guiInput);

    // Compute logical dimensions for panel layout
    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;
    float screenWidth = static_cast<float>(input.width) / scale;
    float screenHeight = static_cast<float>(input.height) / scale;

    // Ensure style scale is always up-to-date (contentScale may not be available during init)
    m_style.scale = scale;

    // Render background grid if visible and there are visible panels (besides status bar)
    // This prevents the gray background from showing when all panels are hidden
    if (m_gridOpacity > 0.0f) {
        // Check if any content panel is visible (excludes status bar)
        const char* contentPanelIds[] = {"nodegraph", "performance", "inspector", "console", "presets"};
        bool hasVisiblePanels = false;
        for (const char* id : contentPanelIds) {
            if (m_panelManager->isPanelVisible(id)) {
                hasVisiblePanels = true;
                break;
            }
        }

        if (hasVisiblePanels) {
            renderBackgroundGrid(*m_canvas, screenWidth, screenHeight);
        }
    }

    m_panelManager->render(*m_canvas, guiInput, screenWidth, screenHeight, m_style);

    // Render preferences dialog (on top of everything)
    if (m_preferencesPanel && m_preferencesPanel->isVisible()) {
        m_preferencesPanel->render(*m_canvas, guiInput, screenWidth, screenHeight, m_style);
    }

    // Render canvas
    m_canvas->render(pass);
}

bool DevTools::consumedInput() const {
    return m_initialized && m_panelManager && m_panelManager->consumedInput();
}

bool DevTools::isInteracting() const {
    return m_initialized && m_panelManager && m_panelManager->isInteracting();
}

void DevTools::onChar(uint32_t codepoint) {
    if (m_panelManager) {
        m_panelManager->onChar(codepoint);
    }
}

bool DevTools::onKeyDown(int key, int mods) {
    if (!m_initialized) return false;

    // If preferences dialog is open, forward input to it
    if (m_preferencesPanel && m_preferencesPanel->isVisible()) {
        if (m_preferencesPanel->onKeyDown(key, mods)) {
            return true;
        }
    }

    // Check shortcuts first (global hotkeys)
    if (m_shortcuts.handleKeyDown(key, mods)) {
        return true;  // Shortcut consumed input
    }

    // Forward to focused panel
    if (m_panelManager) {
        m_panelManager->onKeyDown(key, mods);
    }
    return false;
}

void DevTools::onFullscreenToggle(FullscreenCallback callback) {
    m_fullscreenCallback = std::move(callback);
}

void DevTools::onHelpToggle(HelpCallback callback) {
    m_helpCallback = std::move(callback);
}

void DevTools::showPreferences() {
    if (m_preferencesPanel) {
        m_preferencesPanel->show();
    }
}

void DevTools::hidePreferences() {
    if (m_preferencesPanel) {
        m_preferencesPanel->hide();
    }
}

bool DevTools::isPreferencesVisible() const {
    return m_preferencesPanel && m_preferencesPanel->isVisible();
}

// -------------------------------------------------------------------------
// Panel Control
// -------------------------------------------------------------------------

void DevTools::showPanel(const std::string& panelId) {
    if (m_panelManager) {
        m_panelManager->showPanel(panelId);
    }
}

void DevTools::hidePanel(const std::string& panelId) {
    if (m_panelManager) {
        m_panelManager->hidePanel(panelId);
    }
}

void DevTools::togglePanel(const std::string& panelId) {
    if (m_panelManager) {
        m_panelManager->togglePanel(panelId);
    }
}

bool DevTools::isPanelVisible(const std::string& panelId) const {
    return m_panelManager && m_panelManager->isPanelVisible(panelId);
}

void DevTools::setWindow(GLFWwindow* window) {
    m_window = window;
}

// -------------------------------------------------------------------------
// Visualizer
// -------------------------------------------------------------------------

void DevTools::toggleVisualizer() {
    // NodeGraph is always visible; toggle inspector instead
    togglePanel("inspector");
}

bool DevTools::isVisualizerVisible() const {
    return isPanelVisible("nodegraph");
}

void DevTools::enterSoloMode(Operator* op, const std::string& name) {
    m_soloOperator = op;
    m_soloOperatorName = name;
    m_inSoloMode = true;
}

void DevTools::exitSoloMode() {
    m_soloOperator = nullptr;
    m_soloOperatorName.clear();
    m_inSoloMode = false;
}

void DevTools::updateSoloOutput(Context& ctx) {
    if (!m_inSoloMode || !m_soloOperator) return;

    // Set the output texture to the solo operator's output
    OutputKind kind = m_soloOperator->outputKind();
    if (kind == OutputKind::Texture) {
        WGPUTextureView view = m_soloOperator->outputView();
        if (view) {
            ctx.setOutputTexture(view);
        }
    }
    // TODO: Handle CpuPixels case
}

void DevTools::selectNode(const std::string& name) {
    if (auto* nodeGraph = m_panelManager->getPanelAs<NodeGraphPanel>("nodegraph")) {
        nodeGraph->selectNode(name);
    }
}

void DevTools::setFocusedNode(const std::string& name) {
    if (auto* nodeGraph = m_panelManager->getPanelAs<NodeGraphPanel>("nodegraph")) {
        nodeGraph->setFocusedNode(name);
    }
}

void DevTools::clearFocusedNode() {
    if (auto* nodeGraph = m_panelManager->getPanelAs<NodeGraphPanel>("nodegraph")) {
        nodeGraph->clearFocusedNode();
    }
}

void DevTools::setPendingChangeCount(size_t count) {
    m_pendingChangeCount = count;
    // StatusBar is updated in update()
}

void DevTools::setMcpWarning(const std::string& warning) {
    m_mcpWarning = warning;
    // StatusBar is updated in update()
}

void DevTools::onParamChange(ParamChangeCallback callback) {
    m_paramChangeCallback = std::move(callback);
    // Inspector callback is set in init()
}

void DevTools::setChain(Chain* chain, const std::string& projectDir) {
    if (auto* panel = m_panelManager->getPanelAs<PresetPanel>("presets")) {
        panel->setChain(chain, projectDir);
    }
    if (auto* inspector = m_panelManager->getPanelAs<InspectorPanel>("inspector")) {
        inspector->setChain(chain, projectDir);
    }
    // Load persisted MIDI mappings
    if (chain && !projectDir.empty()) {
        chain->midiMappings().load(projectDir + "/vivid-midi-map.json");
    }
}

// -------------------------------------------------------------------------
// Video/Snapshot Export
// -------------------------------------------------------------------------

void DevTools::saveSnapshot(Context& ctx) {
    m_snapshotRequested = false;
    ctx.snapshot();
}

// -------------------------------------------------------------------------
// Background Grid
// -------------------------------------------------------------------------

void DevTools::toggleBackgroundGrid() {
    // Toggle between 0 and 0.85 (the default visible opacity)
    if (m_gridOpacity > 0.0f) {
        m_gridOpacity = 0.0f;
    } else {
        m_gridOpacity = 0.85f;
    }

    // Sync status bar slider
    if (auto* statusBar = m_panelManager->getPanelAs<StatusBarPanel>("statusbar")) {
        statusBar->setGridOpacity(m_gridOpacity);
    }

    // Save to preferences
    Preferences::instance().setGridOpacity(m_gridOpacity);
}

void DevTools::setGridOpacity(float opacity) {
    m_gridOpacity = std::max(0.0f, std::min(1.0f, opacity));

    // Sync status bar slider
    if (auto* statusBar = m_panelManager->getPanelAs<StatusBarPanel>("statusbar")) {
        statusBar->setGridOpacity(m_gridOpacity);
    }
}

void DevTools::renderBackgroundGrid(OverlayCanvas& canvas, float screenWidth, float screenHeight) {
    // Render at background layer
    canvas.setLayer(UILayer::Background);

    // Semi-transparent dark background - use grid opacity for overall visibility
    glm::vec4 bgColor = m_style.panelBg;
    bgColor.a = m_gridOpacity;
    canvas.fillRect(0, 0, screenWidth, screenHeight, bgColor);

    // Grid parameters
    const float gridSpacing = 40.0f;  // Base grid spacing in logical pixels
    const int majorEvery = 5;         // Major line every N minor lines

    // Scale line colors by grid opacity
    glm::vec4 minorColor = m_style.gridLine;
    minorColor.a *= m_gridOpacity;
    glm::vec4 majorColor = m_style.gridLineMajor;
    majorColor.a *= m_gridOpacity;

    // Draw minor grid lines
    for (float x = 0; x < screenWidth; x += gridSpacing) {
        int lineIndex = static_cast<int>(x / gridSpacing);
        if (lineIndex % majorEvery == 0) continue;  // Skip majors
        canvas.line(x, 0, x, screenHeight, 1.0f, minorColor);
    }
    for (float y = 0; y < screenHeight; y += gridSpacing) {
        int lineIndex = static_cast<int>(y / gridSpacing);
        if (lineIndex % majorEvery == 0) continue;  // Skip majors
        canvas.line(0, y, screenWidth, y, 1.0f, minorColor);
    }

    // Draw major grid lines
    float majorSpacing = gridSpacing * majorEvery;
    for (float x = 0; x < screenWidth; x += majorSpacing) {
        canvas.line(x, 0, x, screenHeight, 1.0f, majorColor);
    }
    for (float y = 0; y < screenHeight; y += majorSpacing) {
        canvas.line(0, y, screenWidth, y, 1.0f, majorColor);
    }
}

} // namespace vivid
