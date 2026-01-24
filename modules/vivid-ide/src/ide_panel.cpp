// IDE Panel Implementation
// Orchestrates terminal and editor panels with tab switching

#include <vivid/ide/ide_panel.h>
#include <vivid/gui/ui_style.h>
#include <vivid/context.h>
#include <effects/font_atlas.h>
#include <vivid/asset_loader.h>
#include <GLFW/glfw3.h>
#include <iostream>

namespace vivid {

// Font storage for IDE panel
static std::unique_ptr<FontAtlas> s_fonts[3];

IdePanel::IdePanel()
    : m_terminal(std::make_unique<TerminalPanel>())
    , m_editor(std::make_unique<EditorPanel>())
    , m_canvas(std::make_unique<OverlayCanvas>())
{
}

IdePanel::~IdePanel() {
    shutdown();
}

bool IdePanel::init(Context& ctx, WGPUTextureFormat surfaceFormat) {
    if (m_initialized) return true;

    // Initialize canvas
    if (!m_canvas->init(ctx.device(), ctx.queue(), surfaceFormat)) {
        std::cerr << "[vivid-ide] Failed to initialize canvas\n";
        return false;
    }

    // Load fonts for text rendering
    auto& assets = AssetLoader::instance();
    std::string regularPath = assets.resolve("fonts/Inter_18pt-Regular.ttf").string();
    std::string mediumPath = assets.resolve("fonts/Inter_18pt-Medium.ttf").string();
    std::string monoPath = assets.resolve("fonts/JetBrainsMono-Regular.ttf").string();

    float scale = ctx.contentScale();
    if (scale < 1.0f) scale = 1.0f;

    // Load Inter Regular (index 0) for labels
    s_fonts[0] = std::make_unique<FontAtlas>();
    if (s_fonts[0]->load(ctx, regularPath, 14.0f * scale)) {
        m_canvas->setFont(0, s_fonts[0].get());
    }

    // Load Inter Medium (index 1) for titles
    s_fonts[1] = std::make_unique<FontAtlas>();
    if (s_fonts[1]->load(ctx, mediumPath, 16.0f * scale)) {
        m_canvas->setFont(1, s_fonts[1].get());
    }

    // Load Roboto Mono (index 2) for terminal/editor
    s_fonts[2] = std::make_unique<FontAtlas>();
    if (s_fonts[2]->load(ctx, monoPath, 14.0f * scale)) {
        m_canvas->setFont(2, s_fonts[2].get());
    }

    // Initialize terminal
    if (!m_terminal->init(80, 24)) {
        std::cerr << "[vivid-ide] Failed to initialize terminal\n";
        return false;
    }

    // Initialize editor
    if (!m_editor->init()) {
        std::cerr << "[vivid-ide] Failed to initialize editor\n";
        return false;
    }

    m_initialized = true;
    return true;
}

void IdePanel::shutdown() {
    if (!m_initialized) return;

    m_terminal->stop();
    m_canvas->cleanup();
    m_initialized = false;
}

void IdePanel::setWorkingDirectory(const std::string& path) {
    m_workingDir = path;

    // Spawn terminal if not already running
    if (!m_terminal->isRunning()) {
        // Estimate terminal size from panel bounds
        // Use approximate char metrics (will be refined in render())
        // Typical 14pt monospace: ~8px wide, ~18px tall at 1x scale
        float contentW = m_bounds.z - 8;  // panel width minus padding
        float contentH = m_bounds.w - 64; // panel height minus title+tab bars
        int cols = std::max(80, static_cast<int>(contentW / 8.0f));
        int rows = std::max(24, static_cast<int>(contentH / 18.0f));
        m_terminal->resize(cols, rows);
        m_terminal->spawn("", path);
    }
}

bool IdePanel::openFile(const std::string& path) {
    return m_editor->openFile(path);
}

void IdePanel::update() {
    if (!m_initialized) return;

    // Update terminal (process PTY output)
    m_terminal->update();
}

void IdePanel::render(WGPURenderPassEncoder pass, const FrameInput& input,
                      float screenWidth, float screenHeight) {
    if (!m_initialized || !m_visible) {
        m_consumedInput = false;
        m_hovered = false;
        return;
    }

    // Use physical pixel dimensions for canvas (like chain_visualizer does)
    // screenWidth/screenHeight are logical, but canvas needs physical for scissor rects
    float physicalWidth = static_cast<float>(input.width);
    float physicalHeight = static_cast<float>(input.height);
    float scale = input.contentScale;

    // Scale bounds to physical pixels for rendering
    glm::vec4 scaledBounds = m_bounds * scale;

    // Handle drag and resize (in logical coordinates)
    handleDragAndResize(input, screenWidth, screenHeight);

    // Clamp bounds to screen (logical)
    m_bounds.x = std::max(0.0f, std::min(m_bounds.x, screenWidth - m_bounds.z));
    m_bounds.y = std::max(0.0f, std::min(m_bounds.y, screenHeight - m_bounds.w));
    m_bounds.z = std::max(200.0f, std::min(m_bounds.z, screenWidth - m_bounds.x));
    m_bounds.w = std::max(150.0f, std::min(m_bounds.w, screenHeight - m_bounds.y));

    // Recalculate scaled bounds after clamping
    scaledBounds = m_bounds * scale;

    // Begin canvas with physical dimensions
    m_canvas->begin(static_cast<int>(physicalWidth), static_cast<int>(physicalHeight));
    m_canvas->setLayer(UILayer::Panels);

    // Use scaled (physical) coordinates for rendering
    float x = scaledBounds.x;
    float y = scaledBounds.y;
    float w = scaledBounds.z;
    float h = scaledBounds.w;

    // Panel chrome (scaled)
    float cornerRadius = 8.0f * scale;
    float titleBarHeight = 28.0f * scale;
    float tabBarHeight = 32.0f * scale;

    // Background with rounded corners
    glm::vec4 bgColor(0.1f, 0.1f, 0.12f, 0.95f);
    glm::vec4 borderColor(0.3f, 0.3f, 0.35f, 1.0f);
    m_canvas->fillRoundedRect(x, y, w, h, cornerRadius, bgColor);
    m_canvas->strokeRoundedRect(x, y, w, h, cornerRadius, 1.0f, borderColor);

    // Title bar
    renderTitleBar(*m_canvas, x, y, w, scale);

    // Tab bar
    renderTabBar(*m_canvas, x, y + titleBarHeight, w, scale);

    // Content area
    float contentY = y + titleBarHeight + tabBarHeight;
    float contentH = h - titleBarHeight - tabBarHeight;
    glm::vec4 contentBounds(x + 4 * scale, contentY, w - 8 * scale, contentH - 4 * scale);

    // Render active panel
    int fontIndex = 2;  // Monospace font
    float lineHeight = m_canvas->fontLineHeight(fontIndex);
    float charWidth = m_canvas->measureText("M", fontIndex);

    if (m_activeTab == IdeTab::Terminal) {
        // Sync terminal size with actual visible area using real font metrics
        if (lineHeight > 0 && charWidth > 0) {
            int cols = static_cast<int>(contentBounds.z / charWidth);
            int rows = static_cast<int>(contentBounds.w / lineHeight);
            cols = std::max(20, cols);
            rows = std::max(5, rows);
            if (cols != m_terminal->cols() || rows != m_terminal->rows()) {
                m_terminal->resize(cols, rows);
            }
        }
        m_terminal->render(*m_canvas, contentBounds, fontIndex);
    } else {
        m_editor->render(*m_canvas, contentBounds, fontIndex);
    }

    // Store content bounds and metrics for mouse click handling
    m_editorContentBounds = contentBounds;
    m_editorLineHeight = lineHeight;
    m_editorCharWidth = charWidth;

    // Compile status indicator
    if (!m_compileSuccess) {
        float statusY = y + h - 24 * scale;
        m_canvas->fillRect(x, statusY, w, 24 * scale, glm::vec4(0.4f, 0.15f, 0.15f, 0.9f));
        m_canvas->text("Compile error", x + 8 * scale, statusY + 16 * scale, glm::vec4(1, 0.5f, 0.5f, 1), 0);
    }

    // Render canvas
    m_canvas->render(pass);

    // Handle input
    m_consumedInput = m_hovered || m_dragging || m_resizing != 0;

    // Always set focus based on active tab (for cursor visibility)
    // Focus is set regardless of hover - input routing is done in app.cpp
    if (m_activeTab == IdeTab::Terminal) {
        m_terminal->setFocused(true);
        m_editor->setFocused(false);
    } else {
        m_terminal->setFocused(false);
        m_editor->setFocused(true);
    }

    // Handle scroll input when hovered
    if (m_hovered && !m_dragging && m_resizing == 0) {
        if (m_activeTab == IdeTab::Terminal) {
            m_terminal->handleInput(input);
        } else {
            m_editor->handleInput(input);
        }
    }
}

void IdePanel::renderTitleBar(OverlayCanvas& canvas, float x, float y, float w, float scale) {
    float titleBarHeight = 28.0f * scale;
    float cornerRadius = 8.0f * scale;

    // Title bar background (only top corners rounded)
    canvas.fillRoundedRectTop(x, y, w, titleBarHeight, cornerRadius, glm::vec4(0.15f, 0.15f, 0.18f, 1.0f));

    // Title text
    std::string title = "Vivid IDE";
    if (!m_compileSuccess) {
        title += " - Error";
    }
    canvas.text(title, x + 10 * scale, y + 18 * scale, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), 0);

    // Drag handle indicator (right side)
    float handleX = x + w - 60 * scale;
    for (int i = 0; i < 3; i++) {
        float dotX = handleX + i * 8 * scale;
        canvas.fillCircle(dotX, y + titleBarHeight / 2, 2 * scale, glm::vec4(0.4f, 0.4f, 0.4f, 1.0f), 8);
    }
}

void IdePanel::renderTabBar(OverlayCanvas& canvas, float x, float y, float w, float scale) {
    float tabBarHeight = 32.0f * scale;
    float tabWidth = 100.0f * scale;
    float tabPadding = 4.0f * scale;

    // Tab bar background
    canvas.fillRect(x, y, w, tabBarHeight, glm::vec4(0.12f, 0.12f, 0.14f, 1.0f));

    // Tabs
    struct TabInfo {
        const char* name;
        IdeTab tab;
    };
    TabInfo tabs[] = {
        {"Terminal", IdeTab::Terminal},
        {"Editor", IdeTab::Editor}
    };

    for (int i = 0; i < 2; i++) {
        float tabX = x + tabPadding + i * (tabWidth + tabPadding);
        float tabY = y + tabPadding;
        float tabH = tabBarHeight - tabPadding * 2;

        bool isActive = (m_activeTab == tabs[i].tab);

        glm::vec4 tabBg = isActive
            ? glm::vec4(0.2f, 0.2f, 0.25f, 1.0f)
            : glm::vec4(0.15f, 0.15f, 0.17f, 1.0f);
        glm::vec4 textColor = isActive
            ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)
            : glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);

        canvas.fillRoundedRect(tabX, tabY, tabWidth, tabH, 4 * scale, tabBg);
        canvas.text(tabs[i].name, tabX + 10 * scale, tabY + tabH - 6 * scale, textColor, 0);
    }

    // Separator line
    canvas.line(x, y + tabBarHeight - scale, x + w, y + tabBarHeight - scale, scale, glm::vec4(0.25f, 0.25f, 0.28f, 1.0f));
}

void IdePanel::handleDragAndResize(const FrameInput& input, float screenW, float screenH) {
    glm::vec2 mousePos = input.mousePos;

    float x = m_bounds.x;
    float y = m_bounds.y;
    float w = m_bounds.z;
    float h = m_bounds.w;
    float hitSize = 8.0f;
    float titleBarHeight = 28.0f;

    // Check if mouse is in panel bounds (with resize margin)
    m_hovered = mousePos.x >= x - hitSize && mousePos.x <= x + w + hitSize &&
                mousePos.y >= y - hitSize && mousePos.y <= y + h + hitSize;

    bool leftMouseDown = input.mouseDown[0];
    // Track clicks by comparing current and previous mouse state
    static bool lastMouseDown = false;
    bool leftMouseClicked = leftMouseDown && !lastMouseDown;
    lastMouseDown = leftMouseDown;

    // Drag area - entire title bar (except tabs area below)
    bool overTitleBar = mousePos.x >= x && mousePos.x <= x + w &&
                        mousePos.y >= y && mousePos.y <= y + titleBarHeight;

    // Start drag from anywhere on title bar
    if (overTitleBar && leftMouseClicked && !m_dragging && m_resizing == 0) {
        m_dragging = true;
        m_dragOffset = mousePos - glm::vec2(x, y);
    }

    // Continue drag
    if (m_dragging) {
        if (leftMouseDown) {
            m_bounds.x = mousePos.x - m_dragOffset.x;
            m_bounds.y = mousePos.y - m_dragOffset.y;
        } else {
            m_dragging = false;
        }
    }

    // Tab click detection
    if (leftMouseClicked && !m_dragging && m_resizing == 0) {
        float tabBarY = y + titleBarHeight;
        float tabBarHeight = 32.0f;
        float tabWidth = 100.0f;
        float tabPadding = 4.0f;

        if (mousePos.y >= tabBarY && mousePos.y <= tabBarY + tabBarHeight) {
            // Terminal tab
            float termTabX = x + tabPadding;
            if (mousePos.x >= termTabX && mousePos.x <= termTabX + tabWidth) {
                setActiveTab(IdeTab::Terminal);
            }
            // Editor tab
            float editorTabX = x + tabPadding + tabWidth + tabPadding;
            if (mousePos.x >= editorTabX && mousePos.x <= editorTabX + tabWidth) {
                setActiveTab(IdeTab::Editor);
            }
        }

        // Editor content click - convert logical mouse pos to physical and pass to editor
        if (m_activeTab == IdeTab::Editor && m_editorLineHeight > 0 && m_editorCharWidth > 0) {
            // m_editorContentBounds is in physical pixels, mousePos is in logical
            float scale = input.contentScale;
            float physicalMouseX = mousePos.x * scale;
            float physicalMouseY = mousePos.y * scale;

            // Check if click is in editor content area
            if (physicalMouseX >= m_editorContentBounds.x &&
                physicalMouseX <= m_editorContentBounds.x + m_editorContentBounds.z &&
                physicalMouseY >= m_editorContentBounds.y &&
                physicalMouseY <= m_editorContentBounds.y + m_editorContentBounds.w) {
                m_editor->onMouseClick(physicalMouseX, physicalMouseY, m_editorContentBounds,
                                       m_editorLineHeight, m_editorCharWidth);
            }
        }
    }

    // Editor mouse drag for selection (while left mouse is held and editor is dragging)
    if (m_activeTab == IdeTab::Editor && leftMouseDown && m_editor->isDragging()) {
        float scale = input.contentScale;
        float physicalMouseX = mousePos.x * scale;
        float physicalMouseY = mousePos.y * scale;
        m_editor->onMouseDrag(physicalMouseX, physicalMouseY);
    }

    // Editor mouse up to finish selection
    if (m_activeTab == IdeTab::Editor && !leftMouseDown && m_editor->isDragging()) {
        m_editor->onMouseUp();
    }

    // Resize detection (edges)
    if (!m_dragging && m_resizing == 0 && leftMouseClicked) {
        int edge = 0;
        if (mousePos.x >= x - hitSize && mousePos.x <= x + hitSize) edge |= 1;  // left
        if (mousePos.x >= x + w - hitSize && mousePos.x <= x + w + hitSize) edge |= 2;  // right
        if (mousePos.y >= y - hitSize && mousePos.y <= y + hitSize) edge |= 4;  // top
        if (mousePos.y >= y + h - hitSize && mousePos.y <= y + h + hitSize) edge |= 8;  // bottom

        if (edge != 0) {
            m_resizing = edge;
            m_resizeStartBounds = m_bounds;
            m_resizeStartMouse = mousePos;
        }
    }

    // Continue resize
    if (m_resizing != 0) {
        if (leftMouseDown) {
            glm::vec2 delta = mousePos - m_resizeStartMouse;
            float newX = m_resizeStartBounds.x;
            float newY = m_resizeStartBounds.y;
            float newW = m_resizeStartBounds.z;
            float newH = m_resizeStartBounds.w;

            if (m_resizing & 1) { newX += delta.x; newW -= delta.x; }  // left
            if (m_resizing & 2) { newW += delta.x; }  // right
            if (m_resizing & 4) { newY += delta.y; newH -= delta.y; }  // top
            if (m_resizing & 8) { newH += delta.y; }  // bottom

            // Minimum size
            float minW = 200.0f;
            float minH = 150.0f;
            if (newW < minW) {
                if (m_resizing & 1) newX = m_resizeStartBounds.x + m_resizeStartBounds.z - minW;
                newW = minW;
            }
            if (newH < minH) {
                if (m_resizing & 4) newY = m_resizeStartBounds.y + m_resizeStartBounds.w - minH;
                newH = minH;
            }

            m_bounds = glm::vec4(newX, newY, newW, newH);
        } else {
            m_resizing = 0;
            // Terminal resize is handled in render() with proper font metrics
        }
    }
}

void IdePanel::setActiveTab(IdeTab tab) {
    m_activeTab = tab;

    // Update focus
    if (tab == IdeTab::Terminal) {
        m_terminal->setFocused(true);
        m_editor->setFocused(false);
    } else {
        m_terminal->setFocused(false);
        m_editor->setFocused(true);
    }
}

void IdePanel::onCompileError(std::function<void(bool, const std::string&)> callback) {
    m_onCompileError = std::move(callback);
}

void IdePanel::setCompileStatus(bool success, const std::string& message) {
    m_compileSuccess = success;
    m_compileMessage = message;

    // Update editor with error info
    if (!success) {
        // Parse error message for line number (format: "file.cpp:42:10: error: ...")
        int errorLine = 0;
        size_t colonPos = message.find(':');
        if (colonPos != std::string::npos) {
            size_t secondColon = message.find(':', colonPos + 1);
            if (secondColon != std::string::npos) {
                std::string lineStr = message.substr(colonPos + 1, secondColon - colonPos - 1);
                try {
                    errorLine = std::stoi(lineStr);
                } catch (...) {}
            }
        }
        m_editor->setError(errorLine, message);
    } else {
        m_editor->clearError();
    }

    if (m_onCompileError) {
        m_onCompileError(success, message);
    }
}

void IdePanel::setWindow(GLFWwindow* window) {
    if (!window) return;

    // Set up clipboard callbacks for the editor
    m_editor->setClipboardCallbacks(
        // Get clipboard
        [window]() -> std::string {
            const char* text = glfwGetClipboardString(window);
            return text ? std::string(text) : std::string();
        },
        // Set clipboard
        [window](const std::string& text) {
            glfwSetClipboardString(window, text.c_str());
        }
    );
}

} // namespace vivid
