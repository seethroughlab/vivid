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
#include <vivid/devtools/dock_zone.h>
#include <vivid/devtools/dock_manager.h>
#include <vivid/devtools/panels/terminal_panel.h>
#include <vivid/devtools/panels/editor_panel.h>
#include <vivid/devtools/panels/node_graph_panel.h>
#include <vivid/devtools/panels/inspector_panel.h>
#include <vivid/devtools/panels/status_bar_panel.h>
#include <vivid/devtools/panels/console_panel.h>
#include <vivid/devtools/panels/file_browser_panel.h>
#include <vivid/context.h>
#include <vivid/asset_loader.h>
#include "effects/font_atlas.h"
#include <GLFW/glfw3.h>
#include <algorithm>
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
    statusBar->onGridOpacityChange([this](float opacity) {
        // Sync with DevTools background grid
        m_gridOpacity = opacity;
        // Save to preferences
        Preferences::instance().setGridOpacity(opacity);
    });
    statusBar->onPanelToggle([this](const std::string& panelId) {
        // Toggle the requested panel
        togglePanel(panelId);
    });
    statusBar->onPanelDrag([this](const std::string& panelId, const glm::vec2& pos) {
        // Drag from status bar button creates floating panel
        if (auto* panel = m_panelManager->getPanel(panelId)) {
            // Show the panel as floating at the drag position
            if (!panel->isVisible()) {
                panel->setVisible(true);
            }
            // Position the panel at the drag location
            glm::vec4 bounds = panel->bounds();
            bounds.x = pos.x - 50;  // Center-ish on cursor
            bounds.y = pos.y;
            bounds.z = std::max(bounds.z, 300.0f);
            bounds.w = std::max(bounds.w, 200.0f);
            panel->setBounds(bounds);

            // Add to floating order and remove from layout
            if (m_panelManager->isLayoutMode() && m_panelManager->dockManager()) {
                m_panelManager->dockManager()->removePanelFromLayout(panel);
                m_panelManager->dockManager()->cleanupEmptyNodes();
            }
            m_panelManager->addToFloatingOrder(panelId);
        }
    });
    statusBar->onPanelDock([this](const std::string& panelId, DockPosition position) {
        // Context menu dock action
        auto* panel = m_panelManager->getPanel(panelId);
        if (!panel) return;

        if (position == DockPosition::None) {
            // Hide the panel
            hidePanel(panelId);
        } else if (position == DockPosition::Float) {
            // Float the panel
            if (!panel->isVisible()) {
                panel->setVisible(true);
            }
            // Remove from layout if present
            if (m_panelManager->isLayoutMode() && m_panelManager->dockManager()) {
                m_panelManager->dockManager()->removePanelFromLayout(panel);
                m_panelManager->dockManager()->cleanupEmptyNodes();
            }
            m_panelManager->addToFloatingOrder(panelId);
        } else {
            // Dock the panel at the specified position
            // This would require more complex logic to insert at a specific position
            // For now, just show the panel and let the user drag it
            showPanel(panelId);
            std::cerr << "[DevTools] Dock to position " << static_cast<int>(position)
                      << " not fully implemented - showing panel instead\n";
        }
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

    // File browser (floating)
    auto fileBrowser = std::make_unique<FileBrowserPanel>();
    fileBrowser->onFileSelected([this](const std::string& path) {
        // Open selected file in editor
        if (auto* ed = m_panelManager->getPanelAs<EditorPanel>("editor")) {
            ed->openFile(path);
            showPanel("editor");
        }
    });
    m_panelManager->addPanel(std::move(fileBrowser));

    // Console (floating - for compile errors)
    auto console = std::make_unique<ConsolePanel>();
    m_panelManager->addPanel(std::move(console));

    // Initialize panel manager (initializes all panels)
    if (!m_panelManager->init(ctx, surfaceFormat)) {
        std::cerr << "[vivid-devtools] Failed to initialize panel manager\n";
        return false;
    }

    // Enable layout mode by default for docking support
    m_panelManager->buildDefaultLayout();
    m_panelManager->setLayoutMode(true);

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

    // Load grid opacity from preferences
    m_gridOpacity = Preferences::instance().gridOpacity();

    // Restore editor session (open files)
    if (auto* ed = m_panelManager->getPanelAs<EditorPanel>("editor")) {
        const auto& openFiles = Preferences::instance().openFiles();
        for (const auto& path : openFiles) {
            ed->openFile(path);
        }
        // Set active file
        const auto& activeFile = Preferences::instance().activeFile();
        if (!activeFile.empty()) {
            for (int i = 0; i < ed->tabCount(); i++) {
                if (ed->tabPath(i) == activeFile) {
                    ed->setActiveTab(i);
                    break;
                }
            }
        }
        // Set callback to save session when tabs change
        ed->onTabChange([](const std::string&) {
            // Session saving is done on shutdown
        });
        ed->onFileSave([](const std::string&) {
            // Could save session here too
        });
    }

    // Restore file browser session (expanded folders)
    if (auto* fb = m_panelManager->getPanelAs<FileBrowserPanel>("filebrowser")) {
        fb->setExpandedFolders(Preferences::instance().expandedFolders());
    }

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

    // Cmd/Ctrl+5: Toggle File Browser
    m_shortcuts.registerShortcut({
        GLFW_KEY_5,
        ShortcutManager::MOD_CMD_OR_CTRL,
        "toggle_filebrowser",
        "Toggle File Browser",
        [this]() { toggleFileBrowser(); }
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

    // Save editor session before shutdown
    if (auto* ed = m_panelManager->getPanelAs<EditorPanel>("editor")) {
        Preferences::instance().setOpenFiles(ed->openFiles());
        if (ed->activeTab() >= 0) {
            Preferences::instance().setActiveFile(ed->tabPath(ed->activeTab()));
        }
    }

    // Save file browser session
    if (auto* fb = m_panelManager->getPanelAs<FileBrowserPanel>("filebrowser")) {
        Preferences::instance().setExpandedFolders(fb->expandedFolders());
    }

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
                             isPanelVisible("inspector") ||
                             isPanelVisible("filebrowser");

    if (auto* statusBar = m_panelManager->getPanelAs<StatusBarPanel>("statusbar")) {
        // Only show status bar if there's visible content OR background grid is shown
        statusBar->setVisible(hasVisibleContent || m_gridOpacity > 0.0f);

        // Sync pending change count
        statusBar->setPendingChangeCount(m_pendingChangeCount);
        statusBar->setMcpWarning(m_mcpWarning);

        // Sync panel dock mode to toggle buttons
        auto getDockMode = [this](const std::string& id) -> DockMode {
            auto* panel = m_panelManager->getPanel(id);
            if (!panel || !panel->isVisible()) {
                return DockMode::Hidden;
            }
            // Check if panel is in floating order
            // If in layout tree, it's docked; otherwise floating
            if (m_panelManager->isLayoutMode() && m_panelManager->layoutRoot()) {
                if (m_panelManager->layoutRoot()->containsPanel(panel)) {
                    return DockMode::Docked;
                }
            }
            return DockMode::Floating;
        };

        statusBar->setPanelDockMode("terminal", getDockMode("terminal"));
        statusBar->setPanelDockMode("console", getDockMode("console"));
        statusBar->setPanelDockMode("editor", getDockMode("editor"));
        statusBar->setPanelDockMode("nodegraph", getDockMode("nodegraph"));
        statusBar->setPanelDockMode("filebrowser", getDockMode("filebrowser"));

        // Sync grid opacity slider
        statusBar->setGridOpacity(m_gridOpacity);
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
    if (m_gridOpacity > 0.0f) {
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

bool DevTools::dockPanel(const std::string& panelId, const std::string& position) {
    if (!m_panelManager) return false;

    Panel* panel = m_panelManager->getPanel(panelId);
    if (!panel) {
        std::cerr << "[DevTools] dockPanel: panel not found: " << panelId << std::endl;
        return false;
    }

    // Parse position string to DockPosition enum
    DockPosition dockPos = DockPosition::None;
    if (position == "left") {
        dockPos = DockPosition::Left;
    } else if (position == "right") {
        dockPos = DockPosition::Right;
    } else if (position == "top") {
        dockPos = DockPosition::Top;
    } else if (position == "bottom") {
        dockPos = DockPosition::Bottom;
    } else if (position == "center") {
        dockPos = DockPosition::Center;
    } else if (position == "float") {
        dockPos = DockPosition::Float;
    } else {
        std::cerr << "[DevTools] dockPanel: invalid position: " << position << std::endl;
        return false;
    }

    // Ensure layout mode is enabled
    if (!m_panelManager->isLayoutMode()) {
        m_panelManager->buildDefaultLayout();
        m_panelManager->setLayoutMode(true);
    }

    // Make the panel visible if it's hidden
    if (!panel->isVisible()) {
        panel->setVisible(true);
    }

    // Use DockManager to dock the panel programmatically
    auto* dm = m_panelManager->dockManager();
    if (!dm) {
        std::cerr << "[DevTools] dockPanel: DockManager not available" << std::endl;
        return false;
    }

    dm->dockPanelProgrammatically(panel, dockPos);
    return true;
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

    // Also set file browser root directory
    if (auto* fileBrowser = m_panelManager->getPanelAs<FileBrowserPanel>("filebrowser")) {
        fileBrowser->setRootDirectory(path);
    }
}

// -------------------------------------------------------------------------
// File Browser
// -------------------------------------------------------------------------

void DevTools::showFileBrowser() {
    showPanel("filebrowser");
}

void DevTools::hideFileBrowser() {
    hidePanel("filebrowser");
}

void DevTools::toggleFileBrowser() {
    togglePanel("filebrowser");
}

bool DevTools::isFileBrowserVisible() const {
    return isPanelVisible("filebrowser");
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
