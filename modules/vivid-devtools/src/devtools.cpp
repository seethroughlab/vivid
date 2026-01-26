// DevTools main orchestrator implementation
//
// This is the unified entry point for all devtools functionality:
// - Terminal panel for shell access
// - Editor panel for chain.cpp editing
// - NodeGraph panel for operator visualization
// - Inspector panel for parameter editing
// - StatusBar panel for record/snapshot controls
//
// Provides backward-compatible APIs for vivid_ide_* and vivid_visualizer_* functions.

#include <vivid/devtools/devtools.h>
#include <vivid/devtools/preferences.h>
#include <vivid/devtools/panels/terminal_panel.h>
#include <vivid/devtools/panels/editor_panel.h>
#include <vivid/devtools/panels/node_graph_panel.h>
#include <vivid/devtools/panels/inspector_panel.h>
#include <vivid/devtools/panels/status_bar_panel.h>
#include <vivid/devtools/panels/console_panel.h>
#include <vivid/context.h>
#include <vivid/asset_loader.h>
#include "effects/font_atlas.h"
#include <GLFW/glfw3.h>
#include <iostream>

namespace vivid {

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

    // Index 2: Terminal/editor - reuse font 0 (same size, same font)
    // This avoids loading a duplicate font atlas
    m_canvas->setFont(2, m_fonts[0].get());

    // Enable HiDPI text mode since fonts are loaded at physical pixel size
    // This makes text(), measureText(), etc. automatically compensate for contentScale
    m_canvas->setHiDPITextMode(true);

    // Initialize panel manager
    m_panelManager = std::make_unique<PanelManager>();

    // Create and add panels
    // Order matters: panels are rendered back-to-front

    // StatusBar (docked at top) - render first (background)
    auto statusBar = std::make_unique<StatusBarPanel>();
    statusBar->setVideoExporter(&m_exporter);
    statusBar->onSnapshot([this]() {
        m_snapshotRequested = true;
    });
    statusBar->onRecord([this](bool start) {
        // TODO: Implement recording toggle
        if (start) {
            std::cerr << "[vivid-devtools] Recording started\n";
        } else {
            std::cerr << "[vivid-devtools] Recording stopped\n";
        }
    });
    statusBar->onGridToggle([this](bool visible) {
        // Sync with DevTools background grid
        m_showBackgroundGrid = visible;
    });
    statusBar->onPanelToggle([this](const std::string& panelId) {
        // Toggle the requested panel
        togglePanel(panelId);
    });
    m_panelManager->addPanel(std::move(statusBar));

    // NodeGraph (fill) - background for visualizer
    auto nodeGraph = std::make_unique<NodeGraphPanel>();
    nodeGraph->onNodeSelect([this](const std::string& name) {
        // When a node is selected, update the inspector and show it
        if (m_ctx) {
            const auto& operators = m_ctx->registeredOperators();
            for (const auto& info : operators) {
                if (info.name == name) {
                    auto* inspector = m_panelManager->getPanelAs<InspectorPanel>("inspector");
                    if (inspector) {
                        inspector->setSelectedOperator(info.op, name);
                        // Auto-show inspector when a node is selected
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

    // Inspector (docked right)
    auto inspector = std::make_unique<InspectorPanel>();
    inspector->onParamChange([this](const std::string& opName, const std::string& paramName,
                                     const float oldVal[4], const float newVal[4], int sourceLine) {
        // Forward to external callback
        if (m_paramChangeCallback) {
            m_paramChangeCallback(opName, paramName, oldVal, newVal, sourceLine);
        }
    });
    m_panelManager->addPanel(std::move(inspector));

    // Terminal (floating)
    auto terminal = std::make_unique<TerminalPanel>();
    m_panelManager->addPanel(std::move(terminal));

    // Editor (floating)
    auto editor = std::make_unique<EditorPanel>();
    m_panelManager->addPanel(std::move(editor));

    // Console (floating - for compile errors)
    auto console = std::make_unique<ConsolePanel>();
    m_panelManager->addPanel(std::move(console));

    // Initialize panel manager (initializes all panels)
    if (!m_panelManager->init(ctx, surfaceFormat)) {
        std::cerr << "[vivid-devtools] Failed to initialize panel manager\n";
        return false;
    }

    // Set up clipboard callbacks for editor if window is set
    if (m_window) {
        setWindow(m_window);
    }

    // Register default keyboard shortcuts
    registerDefaultShortcuts();

    // Create preferences panel
    m_preferencesPanel = std::make_unique<PreferencesPanel>();
    m_preferencesPanel->setStyle(&m_style);
    m_preferencesPanel->setShortcuts(&m_shortcuts);
    m_preferencesPanel->setPanelManager(m_panelManager.get());

    m_initialized = true;
    std::cerr << "[vivid-devtools] Initialized with " << m_panelManager->panelCount() << " panels\n";
    return true;
}

void DevTools::registerDefaultShortcuts() {
    // Cmd/Ctrl+1: Toggle Terminal
    m_shortcuts.registerShortcut({
        GLFW_KEY_1,
        ShortcutManager::MOD_CMD_OR_CTRL,
        "toggle_terminal",
        "Toggle Terminal",
        [this]() { togglePanel("terminal"); }
    });

    // Cmd/Ctrl+2: Toggle Console
    m_shortcuts.registerShortcut({
        GLFW_KEY_2,
        ShortcutManager::MOD_CMD_OR_CTRL,
        "toggle_console",
        "Toggle Console",
        [this]() { togglePanel("console"); }
    });

    // Cmd/Ctrl+3: Toggle Editor
    m_shortcuts.registerShortcut({
        GLFW_KEY_3,
        ShortcutManager::MOD_CMD_OR_CTRL,
        "toggle_editor",
        "Toggle Editor",
        [this]() { togglePanel("editor"); }
    });

    // Cmd/Ctrl+4: Toggle Chain Visualizer (nodegraph + inspector + statusbar)
    m_shortcuts.registerShortcut({
        GLFW_KEY_4,
        ShortcutManager::MOD_CMD_OR_CTRL,
        "toggle_visualizer",
        "Toggle Chain Visualizer",
        [this]() { toggleVisualizer(); }
    });

    // Note: Cmd/Ctrl+F (fullscreen) is now a built-in shortcut in vivid-core,
    // so it works even without devtools loaded.

    // F1 or Cmd/Ctrl+?: Show Help
    m_shortcuts.registerShortcut({
        GLFW_KEY_F1,
        0,  // No modifiers
        "show_help",
        "Show Shortcuts",
        [this]() {
            if (m_helpCallback) {
                m_helpCallback();
            }
        }
    });

    // Cmd/Ctrl+L: Toggle Layout Mode (experimental docking)
    m_shortcuts.registerShortcut({
        GLFW_KEY_L,
        ShortcutManager::MOD_CMD_OR_CTRL,
        "toggle_layout_mode",
        "Toggle Layout Mode",
        [this]() {
            if (m_panelManager) {
                bool newMode = !m_panelManager->isLayoutMode();
                if (newMode && !m_panelManager->layoutRoot()) {
                    // Build default layout if not yet created
                    m_panelManager->buildDefaultLayout();
                    std::cerr << "[vivid-devtools] Built default layout\n";
                }
                m_panelManager->setLayoutMode(newMode);
                std::cerr << "[vivid-devtools] Layout mode: " << (newMode ? "ON" : "OFF") << "\n";
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

    // Cmd/Ctrl+G: Toggle Background Grid
    m_shortcuts.registerShortcut({
        GLFW_KEY_G,
        ShortcutManager::MOD_CMD_OR_CTRL,
        "toggle_grid",
        "Toggle Background Grid",
        [this]() { toggleBackgroundGrid(); }
    });
}

void DevTools::shutdown() {
    if (!m_initialized) return;

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

    // Auto-hide status bar when no content panels are visible
    bool hasVisibleContent = isPanelVisible("terminal") ||
                             isPanelVisible("editor") ||
                             isPanelVisible("console") ||
                             isPanelVisible("nodegraph") ||
                             isPanelVisible("inspector");

    if (auto* statusBar = m_panelManager->getPanelAs<StatusBarPanel>("statusbar")) {
        // Only show status bar if there's visible content OR background grid is shown
        statusBar->setVisible(hasVisibleContent || m_showBackgroundGrid);

        // Sync pending change count
        statusBar->setPendingChangeCount(m_pendingChangeCount);
        statusBar->setMcpWarning(m_mcpWarning);

        // Sync panel visibility state to toggle buttons
        statusBar->setPanelVisibility("terminal", isPanelVisible("terminal"));
        statusBar->setPanelVisibility("console", isPanelVisible("console"));
        statusBar->setPanelVisibility("editor", isPanelVisible("editor"));
        statusBar->setPanelVisibility("nodegraph", isPanelVisible("nodegraph"));
        statusBar->setPanelVisibility("grid", m_showBackgroundGrid);
    }
}

void DevTools::render(WGPURenderPassEncoder pass, const FrameInput& input, Context& ctx) {
    if (!m_initialized) return;

    // Begin canvas - handles HiDPI scaling internally
    m_canvas->begin(input);

    // Compute logical dimensions for panel layout
    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;
    float screenWidth = static_cast<float>(input.width) / scale;
    float screenHeight = static_cast<float>(input.height) / scale;

    // Ensure style scale is always up-to-date (contentScale may not be available during init)
    m_style.scale = scale;

    // Render background grid if visible (before panels)
    if (m_showBackgroundGrid) {
        renderBackgroundGrid(*m_canvas, screenWidth, screenHeight);
    }

    m_panelManager->render(*m_canvas, input, screenWidth, screenHeight, m_style);

    // Render preferences dialog (on top of everything)
    if (m_preferencesPanel && m_preferencesPanel->isVisible()) {
        m_preferencesPanel->render(*m_canvas, input, screenWidth, screenHeight, m_style);
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

// -------------------------------------------------------------------------
// IDE Backward Compatibility
// -------------------------------------------------------------------------

void DevTools::toggleIde() {
    // Toggle both terminal and editor panels
    bool currentlyVisible = isIdeVisible();
    if (currentlyVisible) {
        hidePanel("terminal");
        hidePanel("editor");
    } else {
        showPanel("terminal");
        showPanel("editor");
    }
}

bool DevTools::isIdeVisible() const {
    return isPanelVisible("terminal") || isPanelVisible("editor");
}

void DevTools::setIdeVisible(bool visible) {
    if (visible) {
        showPanel("terminal");
        showPanel("editor");
    } else {
        hidePanel("terminal");
        hidePanel("editor");
    }
}

void DevTools::setWorkingDirectory(const std::string& path) {
    if (auto* terminal = m_panelManager->getPanelAs<TerminalPanel>("terminal")) {
        // Spawn shell in the working directory
        terminal->spawn("", path);
    }
}

bool DevTools::openFile(const std::string& path) {
    if (auto* editor = m_panelManager->getPanelAs<EditorPanel>("editor")) {
        return editor->openFile(path);
    }
    return false;
}

void DevTools::setCompileStatus(bool success, const std::string& message) {
    if (auto* editor = m_panelManager->getPanelAs<EditorPanel>("editor")) {
        if (success) {
            editor->clearError();
        } else {
            // Parse line number from error message if possible
            // Format: "filename:line:col: error: message"
            int line = 1;
            size_t colonPos = message.find(':');
            if (colonPos != std::string::npos) {
                size_t secondColon = message.find(':', colonPos + 1);
                if (secondColon != std::string::npos) {
                    std::string lineStr = message.substr(colonPos + 1, secondColon - colonPos - 1);
                    try {
                        line = std::stoi(lineStr);
                    } catch (...) {}
                }
            }
            editor->setError(line, message);
        }
    }
}

void DevTools::setWindow(GLFWwindow* window) {
    m_window = window;

    // Set up clipboard callbacks for editor and console
    if (m_panelManager && window) {
        auto getClipboard = [window]() -> std::string {
            const char* text = glfwGetClipboardString(window);
            return text ? text : "";
        };
        auto setClipboard = [window](const std::string& text) {
            glfwSetClipboardString(window, text.c_str());
        };

        if (auto* editor = m_panelManager->getPanelAs<EditorPanel>("editor")) {
            editor->setClipboardCallbacks(getClipboard, setClipboard);
        }

        if (auto* console = m_panelManager->getPanelAs<ConsolePanel>("console")) {
            console->setClipboardCallbacks(getClipboard, setClipboard);
        }
    }
}

glm::vec4 DevTools::getIdeBounds() const {
    // Return combined bounds of terminal/editor panels
    if (auto* terminal = m_panelManager->getPanelAs<TerminalPanel>("terminal")) {
        return terminal->bounds();
    }
    return {20, 60, 900, 600};
}

void DevTools::setIdeBounds(const glm::vec4& bounds) {
    // Only set terminal bounds - editor is now a separate panel
    if (auto* terminal = m_panelManager->getPanelAs<TerminalPanel>("terminal")) {
        terminal->setBounds(bounds);
    }
}

// -------------------------------------------------------------------------
// Visualizer Backward Compatibility
// -------------------------------------------------------------------------

void DevTools::toggleVisualizer() {
    // Toggle nodegraph + inspector + console panels
    // Status bar is auto-managed based on whether any content panels are visible
    bool currentlyVisible = isVisualizerVisible();
    if (currentlyVisible) {
        hidePanel("nodegraph");
        hidePanel("inspector");
        hidePanel("console");
    } else {
        showPanel("nodegraph");
        showPanel("inspector");
        // Show console only if it has errors
        if (auto* console = m_panelManager->getPanelAs<ConsolePanel>("console")) {
            if (console->hasErrors()) {
                showPanel("console");
            }
        }
    }
}

bool DevTools::isVisualizerVisible() const {
    return isPanelVisible("nodegraph") || isPanelVisible("inspector");
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

// -------------------------------------------------------------------------
// Console
// -------------------------------------------------------------------------

void DevTools::setCompileErrors(const std::vector<CompileError>& errors) {
    if (auto* console = m_panelManager->getPanelAs<ConsolePanel>("console")) {
        console->setCompileErrors(errors);
    }
}

void DevTools::clearCompileErrors() {
    if (auto* console = m_panelManager->getPanelAs<ConsolePanel>("console")) {
        console->clearCompileErrors();
    }
}

void DevTools::addConsoleMessage(int type, const std::string& message) {
    if (auto* console = m_panelManager->getPanelAs<ConsolePanel>("console")) {
        ConsoleMessageType msgType;
        switch (type) {
            case 0: msgType = ConsoleMessageType::Info; break;
            case 1: msgType = ConsoleMessageType::Warning; break;
            case 2: msgType = ConsoleMessageType::Error; break;
            case 3: msgType = ConsoleMessageType::Debug; break;
            default: msgType = ConsoleMessageType::Info; break;
        }
        console->addMessage(msgType, message);
    }
}

void DevTools::clearConsole() {
    if (auto* console = m_panelManager->getPanelAs<ConsolePanel>("console")) {
        console->clear();
    }
}

bool DevTools::consoleHasErrors() const {
    if (auto* console = m_panelManager->getPanelAs<ConsolePanel>("console")) {
        return console->hasErrors();
    }
    return false;
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
    m_showBackgroundGrid = !m_showBackgroundGrid;

    // Sync status bar checkbox
    if (auto* statusBar = m_panelManager->getPanelAs<StatusBarPanel>("statusbar")) {
        statusBar->setGridVisible(m_showBackgroundGrid);
    }
}

void DevTools::renderBackgroundGrid(OverlayCanvas& canvas, float screenWidth, float screenHeight) {
    // Render at background layer
    canvas.setLayer(UILayer::Background);

    // Semi-transparent dark background
    glm::vec4 bgColor = m_style.panelBg;
    bgColor.a = 0.85f;  // Slightly more transparent
    canvas.fillRect(0, 0, screenWidth, screenHeight, bgColor);

    // Grid parameters
    const float gridSpacing = 40.0f;  // Base grid spacing in logical pixels
    const int majorEvery = 5;         // Major line every N minor lines

    // Draw minor grid lines
    for (float x = 0; x < screenWidth; x += gridSpacing) {
        int lineIndex = static_cast<int>(x / gridSpacing);
        if (lineIndex % majorEvery == 0) continue;  // Skip majors
        canvas.line(x, 0, x, screenHeight, 1.0f, m_style.gridLine);
    }
    for (float y = 0; y < screenHeight; y += gridSpacing) {
        int lineIndex = static_cast<int>(y / gridSpacing);
        if (lineIndex % majorEvery == 0) continue;  // Skip majors
        canvas.line(0, y, screenWidth, y, 1.0f, m_style.gridLine);
    }

    // Draw major grid lines
    float majorSpacing = gridSpacing * majorEvery;
    for (float x = 0; x < screenWidth; x += majorSpacing) {
        canvas.line(x, 0, x, screenHeight, 1.0f, m_style.gridLineMajor);
    }
    for (float y = 0; y < screenHeight; y += majorSpacing) {
        canvas.line(0, y, screenWidth, y, 1.0f, m_style.gridLineMajor);
    }
}

} // namespace vivid
