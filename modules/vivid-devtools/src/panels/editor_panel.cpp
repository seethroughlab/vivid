// Editor Panel Implementation
// Multi-file tabbed editor with syntax highlighting
// Migrated from vivid-ide to vivid-devtools

#include <vivid/devtools/panels/editor_panel.h>
#include <vivid/devtools/file_buffer.h>
#include <vivid/context.h>
#include <vivid/gui/ui_style.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace vivid {

// Simple syntax highlighting keywords
static const std::vector<std::string> CPP_KEYWORDS = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "int", "long", "register", "return", "short", "signed", "sizeof", "static",
    "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
    "class", "public", "private", "protected", "virtual", "override", "namespace",
    "using", "template", "typename", "new", "delete", "true", "false", "nullptr",
    "bool", "constexpr", "inline", "explicit", "final", "noexcept"
};

static const std::vector<std::string> WGSL_KEYWORDS = {
    "fn", "var", "let", "const", "struct", "if", "else", "for", "while", "loop",
    "return", "break", "continue", "discard", "switch", "case", "default",
    "true", "false", "vec2", "vec3", "vec4", "mat2x2", "mat3x3", "mat4x4",
    "f32", "i32", "u32", "bool", "sampler", "texture_2d", "uniform", "storage"
};

struct EditorPanel::Impl {
    // All open file buffers (tabs)
    std::vector<std::unique_ptr<FileBuffer>> buffers;
    int activeIndex = -1;  // -1 means no file open

    UIStyle style;

    // Clipboard callbacks
    ClipboardGetCallback getClipboard;
    ClipboardSetCallback setClipboard;
    std::string internalClipboard;  // Fallback

    // File save callback
    FileSaveCallback onSave;

    // Tab change callback
    TabChangeCallback onTabChange;

    // Mouse drag state
    bool isDragging = false;
    glm::vec4 lastBounds = {0, 0, 0, 0};
    float lastLineHeight = 0;
    float lastCharWidth = 0;

    // Tab bar state
    float tabScrollOffset = 0;  // Horizontal scroll for many tabs
    int hoveredTab = -1;
    int hoveredCloseButton = -1;
    bool tabClosePressed = false;

    // Active buffer accessor (may return nullptr)
    FileBuffer* active() {
        if (activeIndex >= 0 && activeIndex < static_cast<int>(buffers.size())) {
            return buffers[activeIndex].get();
        }
        return nullptr;
    }

    const FileBuffer* active() const {
        if (activeIndex >= 0 && activeIndex < static_cast<int>(buffers.size())) {
            return buffers[activeIndex].get();
        }
        return nullptr;
    }

    // Find buffer by path (-1 if not found)
    int findBuffer(const std::string& path) const {
        for (size_t i = 0; i < buffers.size(); i++) {
            if (buffers[i]->path() == path) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool isKeyword(const std::string& word, bool wgsl) {
        const auto& keywords = wgsl ? WGSL_KEYWORDS : CPP_KEYWORDS;
        return std::find(keywords.begin(), keywords.end(), word) != keywords.end();
    }
};

EditorPanel::EditorPanel() {
    m_config.id = "editor";
    m_config.title = "Editor";
    m_config.bounds = {500, 60, 800, 600};  // Offset from terminal
    m_config.dockSide = DockSide::None;
    m_config.visible = false;
    m_config.resizable = true;
    m_config.draggable = true;
    m_config.minWidth = 400.0f;
    m_config.minHeight = 200.0f;
}

EditorPanel::~EditorPanel() = default;

bool EditorPanel::init(Context& ctx, WGPUTextureFormat surfaceFormat) {
    m_impl = std::make_unique<Impl>();
    m_impl->style.scale = ctx.contentScale();
    return true;
}

void EditorPanel::shutdown() {
    m_impl.reset();
}

void EditorPanel::renderTabBar(OverlayCanvas& canvas, float x, float y, float w,
                                const gui::InputState& input, const UIStyle& style) {
    if (!m_impl || m_impl->buffers.empty()) return;

    const float tabHeight = 28.0f;
    const float tabPadding = 8.0f;
    const float closeButtonSize = 14.0f;
    const float maxTabWidth = 180.0f;
    const float minTabWidth = 80.0f;

    int fontIndex = 0;  // Labels font

    // Tab bar background
    canvas.fillRect(x, y, w, tabHeight, style.panelBg * 0.9f);

    // Calculate tab widths
    float totalTabWidth = 0;
    std::vector<float> tabWidths;
    for (const auto& buf : m_impl->buffers) {
        std::string name = buf->filename();
        if (buf->isDirty()) name += "*";
        float textWidth = canvas.measureText(name, fontIndex);
        float tabW = std::min(maxTabWidth, std::max(minTabWidth, textWidth + tabPadding * 3 + closeButtonSize));
        tabWidths.push_back(tabW);
        totalTabWidth += tabW;
    }

    // Render tabs
    float tabX = x - m_impl->tabScrollOffset;
    canvas.beginClipRect(x, y, w, tabHeight);

    for (size_t i = 0; i < m_impl->buffers.size(); i++) {
        auto& buf = m_impl->buffers[i];
        float tabW = tabWidths[i];
        bool isActive = (static_cast<int>(i) == m_impl->activeIndex);
        bool isHovered = (static_cast<int>(i) == m_impl->hoveredTab);

        // Tab background
        glm::vec4 tabBg = isActive ? style.panelBg : (isHovered ? style.panelBg * 0.8f : style.panelBg * 0.6f);
        canvas.fillRect(tabX, y, tabW - 1, tabHeight, tabBg);

        // Active tab indicator (bottom border)
        if (isActive) {
            canvas.fillRect(tabX, y + tabHeight - 2, tabW - 1, 2, style.accent);
        }

        // Tab text
        std::string name = buf->filename();
        if (buf->isDirty()) name += "*";
        glm::vec4 textColor = isActive ? style.textPrimary : style.textDim;
        canvas.text(name, tabX + tabPadding, y + tabHeight - 8, textColor, fontIndex);

        // Close button (x)
        float closeX = tabX + tabW - closeButtonSize - tabPadding;
        float closeY = y + (tabHeight - closeButtonSize) / 2;
        bool closeHovered = (static_cast<int>(i) == m_impl->hoveredCloseButton);

        if (isHovered || isActive) {
            glm::vec4 closeBg = closeHovered ? style.buttonHover : glm::vec4(0, 0, 0, 0);
            canvas.fillRect(closeX - 2, closeY - 2, closeButtonSize + 4, closeButtonSize + 4, closeBg);
            canvas.text("x", closeX + 2, closeY + closeButtonSize - 3,
                        closeHovered ? style.textPrimary : style.textDim, fontIndex);
        }

        tabX += tabW;
    }

    canvas.endClipRect();

    // Tab bar bottom border
    canvas.fillRect(x, y + tabHeight - 1, w, 1, style.panelBorder);
}

bool EditorPanel::handleTabBarInput(const gui::InputState& input, float x, float y, float w, float tabHeight) {
    if (!m_impl) return false;

    m_impl->hoveredTab = -1;
    m_impl->hoveredCloseButton = -1;

    if (m_impl->buffers.empty()) return false;

    // Check if mouse is in tab bar
    if (input.mousePos.y < y || input.mousePos.y > y + tabHeight) {
        return false;
    }
    if (input.mousePos.x < x || input.mousePos.x > x + w) {
        return false;
    }

    const float tabPadding = 8.0f;
    const float closeButtonSize = 14.0f;
    const float maxTabWidth = 180.0f;
    const float minTabWidth = 80.0f;

    // Find which tab is hovered
    float tabX = x - m_impl->tabScrollOffset;
    for (size_t i = 0; i < m_impl->buffers.size(); i++) {
        auto& buf = m_impl->buffers[i];
        std::string name = buf->filename();
        if (buf->isDirty()) name += "*";
        // Approximate text width (we don't have canvas here)
        float textWidth = name.length() * 8.0f;
        float tabW = std::min(maxTabWidth, std::max(minTabWidth, textWidth + tabPadding * 3 + closeButtonSize));

        if (input.mousePos.x >= tabX && input.mousePos.x < tabX + tabW) {
            m_impl->hoveredTab = static_cast<int>(i);

            // Check close button
            float closeX = tabX + tabW - closeButtonSize - tabPadding;
            float closeY = y + (tabHeight - closeButtonSize) / 2;
            if (input.mousePos.x >= closeX - 2 && input.mousePos.x <= closeX + closeButtonSize + 2 &&
                input.mousePos.y >= closeY - 2 && input.mousePos.y <= closeY + closeButtonSize + 2) {
                m_impl->hoveredCloseButton = static_cast<int>(i);
            }
            break;
        }
        tabX += tabW;
    }

    // Handle click
    if (input.mouseClicked[0]) {
        if (m_impl->hoveredCloseButton >= 0) {
            // Close tab
            closeFile(m_impl->hoveredCloseButton);
            return true;
        } else if (m_impl->hoveredTab >= 0) {
            // Switch tab
            setActiveTab(m_impl->hoveredTab);
            return true;
        }
    }

    return m_impl->hoveredTab >= 0;
}

void EditorPanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                         const gui::InputState& input, const UIStyle& style) {
    if (!m_config.visible || !m_impl) {
        m_inputRouting.consumedInput = false;
        m_focus.hovered = false;
        return;
    }

    // Store style for use throughout render
    m_impl->style = style;

    glm::vec4 renderBounds = beginRender(input, bounds);
    float x = renderBounds.x;
    float y = renderBounds.y;
    float w = renderBounds.z;
    float h = renderBounds.w;

    // Panel chrome
    float titleBarHeight = m_display.showTitleBar ? style.titleBarHeight() : 0.0f;
    float tabBarHeight = 28.0f;
    float contentY = y + titleBarHeight;
    float contentH = h - titleBarHeight;

    // Render chrome (title bar controlled by m_display.showTitleBar)
    renderChrome(canvas, x, y, w, h, style, m_display.showTitleBar, &input);

    // Render tab bar (if we have files)
    if (!m_impl->buffers.empty()) {
        renderTabBar(canvas, x, contentY, w, input, style);
        contentY += tabBarHeight;
        contentH -= tabBarHeight;
    }

    // Handle tab bar input
    if (m_focus.hovered && !m_impl->buffers.empty()) {
        handleTabBarInput(input, x, y + titleBarHeight, w, tabBarHeight);
    }

    // Get font metrics (monospace font at index 2)
    int fontIndex = 2;
    float lineHeight = canvas.fontLineHeight(fontIndex);
    float charWidth = canvas.measureText("M", fontIndex);

    if (lineHeight <= 0 || charWidth <= 0) {
        lineHeight = 16.0f;
        charWidth = 8.0f;
    }

    // Store metrics for mouse handling
    m_impl->lastLineHeight = lineHeight;
    m_impl->lastCharWidth = charWidth;

    // Content area
    float padding = 4;
    float contentX = x + padding;
    float contentW = w - padding * 2;
    contentY += padding;
    contentH -= padding;

    m_impl->lastBounds = {contentX, contentY, contentW, contentH};

    // Get active buffer
    FileBuffer* buf = m_impl->active();
    if (!buf) {
        // No file open - show placeholder
        canvas.fillRect(contentX, contentY, contentW, contentH, style.panelBg);
        canvas.text("No file open", contentX + contentW / 2 - 40, contentY + contentH / 2,
                    style.textDim, fontIndex);
        return;
    }

    // Colors from style
    glm::vec4 bgColor = style.panelBg;
    glm::vec4 gutterColor = style.editorGutter;
    glm::vec4 lineNumColor = style.editorLineNum;
    glm::vec4 textColor = style.textPrimary;
    glm::vec4 keywordColor = style.syntaxKeyword;
    glm::vec4 commentColor = style.syntaxComment;
    glm::vec4 stringColor = style.syntaxString;
    glm::vec4 numberColor = style.syntaxNumber;
    glm::vec4 errorLineColor = style.editorErrorLine;
    glm::vec4 cursorLineColor = style.editorCursorLine;
    glm::vec4 selectionColor = style.editorSelection;

    // Get normalized selection bounds
    int selStartLine = 0, selStartCol = 0, selEndLine = 0, selEndCol = 0;
    if (buf->hasSelection) {
        buf->getNormalizedSelection(selStartLine, selStartCol, selEndLine, selEndCol);
    }

    // Line number gutter width
    int lineCount = static_cast<int>(buf->lines().size());
    int gutterDigits = 1;
    int temp = lineCount;
    while (temp >= 10) { gutterDigits++; temp /= 10; }
    float gutterWidth = (gutterDigits + 2) * charWidth;

    // Background
    canvas.fillRect(contentX, contentY, contentW, contentH, bgColor);
    canvas.fillRect(contentX, contentY, gutterWidth, contentH, gutterColor);

    // Clip content area
    canvas.beginClipRect(contentX, contentY, contentW, contentH);

    // Calculate visible lines
    int visibleLines = static_cast<int>(contentH / lineHeight) + 1;
    int startLine = buf->scrollOffset;
    int endLine = std::min(startLine + visibleLines, lineCount);

    bool isWgsl = buf->isWgsl();

    for (int i = startLine; i < endLine; i++) {
        float lineY = contentY + (i - startLine) * lineHeight;

        // Current line highlight (only if no selection)
        if (i == buf->cursorLine && m_focus.focused && !buf->hasSelection) {
            canvas.fillRect(contentX + gutterWidth, lineY, contentW - gutterWidth, lineHeight, cursorLineColor);
        }

        // Error line highlight
        if (i + 1 == buf->errorLine) {
            canvas.fillRect(contentX + gutterWidth, lineY, contentW - gutterWidth, lineHeight, errorLineColor);
        }

        // Selection highlight
        if (buf->hasSelection && i >= selStartLine && i <= selEndLine) {
            float textX = contentX + gutterWidth + charWidth;
            int lineLen = static_cast<int>(buf->lines()[i].size());

            int selColStart = 0;
            int selColEnd = lineLen;

            if (i == selStartLine) {
                selColStart = selStartCol;
            }
            if (i == selEndLine) {
                selColEnd = selEndCol;
            }

            float selX = textX + selColStart * charWidth;
            float selW = (selColEnd - selColStart) * charWidth;
            if (selW > 0) {
                canvas.fillRect(selX, lineY, selW, lineHeight, selectionColor);
            }
        }

        // Line number
        char lineNum[16];
        snprintf(lineNum, sizeof(lineNum), "%*d", gutterDigits, i + 1);
        canvas.text(lineNum, contentX + charWidth, lineY + lineHeight - 4, lineNumColor, fontIndex);

        // Line text with basic syntax highlighting
        const std::string& line = buf->lines()[i];
        float textX = contentX + gutterWidth + charWidth;

        bool inString = false;
        char stringChar = 0;
        size_t wordStart = 0;
        bool inWord = false;

        for (size_t j = 0; j <= line.size(); j++) {
            char c = (j < line.size()) ? line[j] : ' ';

            // Check for comment start
            if (!inString && j + 1 < line.size() && line[j] == '/' && line[j+1] == '/') {
                if (inWord) {
                    std::string word = line.substr(wordStart, j - wordStart);
                    glm::vec4 color = m_impl->isKeyword(word, isWgsl) ? keywordColor : textColor;
                    canvas.text(word, textX + wordStart * charWidth, lineY + lineHeight - 4, color, fontIndex);
                    inWord = false;
                }
                // Render rest of line as comment
                std::string comment = line.substr(j);
                canvas.text(comment, textX + j * charWidth, lineY + lineHeight - 4, commentColor, fontIndex);
                break;
            }

            // Check for string start/end
            if ((c == '"' || c == '\'')) {
                if (!inString) {
                    if (inWord) {
                        std::string word = line.substr(wordStart, j - wordStart);
                        glm::vec4 color = m_impl->isKeyword(word, isWgsl) ? keywordColor : textColor;
                        canvas.text(word, textX + wordStart * charWidth, lineY + lineHeight - 4, color, fontIndex);
                        inWord = false;
                    }
                    inString = true;
                    stringChar = c;
                    wordStart = j;
                } else if (c == stringChar) {
                    std::string str = line.substr(wordStart, j - wordStart + 1);
                    canvas.text(str, textX + wordStart * charWidth, lineY + lineHeight - 4, stringColor, fontIndex);
                    inString = false;
                    wordStart = j + 1;
                }
                continue;
            }

            if (inString) continue;

            // Word boundaries
            bool isWordChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '_';

            if (isWordChar && !inWord) {
                wordStart = j;
                inWord = true;
            } else if (!isWordChar && inWord) {
                std::string word = line.substr(wordStart, j - wordStart);

                // Check if it's a number
                bool isNumber = true;
                for (char wc : word) {
                    if (!((wc >= '0' && wc <= '9') || wc == '.' || wc == 'f' || wc == 'x')) {
                        isNumber = false;
                        break;
                    }
                }

                glm::vec4 color = textColor;
                if (isNumber && word[0] >= '0' && word[0] <= '9') {
                    color = numberColor;
                } else if (m_impl->isKeyword(word, isWgsl)) {
                    color = keywordColor;
                }

                canvas.text(word, textX + wordStart * charWidth, lineY + lineHeight - 4, color, fontIndex);
                inWord = false;
            }

            // Render non-word characters (operators, punctuation)
            if (!isWordChar && !inString && c != ' ' && j < line.size()) {
                char ch[2] = {c, 0};
                canvas.text(ch, textX + j * charWidth, lineY + lineHeight - 4, textColor, fontIndex);
            }
        }
    }

    // Draw cursor
    if (m_focus.focused && buf->cursorLine >= startLine && buf->cursorLine < endLine) {
        float cursorX = contentX + gutterWidth + charWidth + buf->cursorCol * charWidth;
        float cursorY = contentY + (buf->cursorLine - startLine) * lineHeight;
        canvas.fillRect(cursorX, cursorY, 2, lineHeight, style.terminalCursor);
    }

    canvas.endClipRect();

    // Error message at bottom
    if (!buf->errorMessage.empty()) {
        float errorY = y + h - lineHeight - 4;
        canvas.fillRect(x, errorY, w, lineHeight + 4, style.editorErrorLine);
        canvas.text(buf->errorMessage, x + 8, errorY + lineHeight - 2, style.error, fontIndex);
    }
}

bool EditorPanel::handleInput(const gui::InputState& input) {
    if (!m_focus.focused || !m_impl) return false;

    FileBuffer* buf = m_impl->active();
    if (!buf) return false;

    // Handle scroll
    if (input.scroll.y != 0) {
        buf->scroll(static_cast<int>(-input.scroll.y * 3));
        return true;
    }

    // Get stored bounds and metrics from render pass
    glm::vec4 contentBounds = m_impl->lastBounds;
    float lineHeight = m_impl->lastLineHeight;
    float charWidth = m_impl->lastCharWidth;

    // Check if mouse is in content area
    bool mouseInContent = input.mousePos.x >= contentBounds.x &&
                          input.mousePos.x <= contentBounds.x + contentBounds.z &&
                          input.mousePos.y >= contentBounds.y &&
                          input.mousePos.y <= contentBounds.y + contentBounds.w;

    // Handle mouse selection
    if (mouseInContent && input.mouseClicked[0]) {
        onMouseClick(input.mousePos.x, input.mousePos.y, contentBounds, lineHeight, charWidth);
        return true;
    } else if (m_editorDragging && input.mouseDown[0]) {
        onMouseDrag(input.mousePos.x, input.mousePos.y);
        return true;
    } else if (m_editorDragging && !input.mouseDown[0]) {
        onMouseUp();
        return true;
    }

    return false;
}

void EditorPanel::onChar(uint32_t codepoint) {
    if (!m_impl) return;
    FileBuffer* buf = m_impl->active();
    if (!buf) return;

    if (codepoint < 32 || codepoint > 126) return;

    // Delete selection if any
    if (buf->hasSelection) {
        buf->deleteSelection();
    }

    buf->ensureLine(buf->cursorLine);
    std::string& line = buf->lines()[buf->cursorLine];

    char c = static_cast<char>(codepoint);
    line.insert(buf->cursorCol, 1, c);
    buf->cursorCol++;
    buf->setDirty(true);
    buf->clampCursor();
}

void EditorPanel::onKeyDown(int key, int mods) {
    if (!m_impl) return;

    bool ctrl = (mods & 0x2) != 0;   // GLFW_MOD_CONTROL
    bool shift = (mods & 0x1) != 0;  // GLFW_MOD_SHIFT
    bool super = (mods & 0x8) != 0;  // GLFW_MOD_SUPER (Cmd on macOS)
    bool cmdOrCtrl = ctrl || super;

    // Tab management shortcuts
    // Cmd+W: Close current tab
    if (cmdOrCtrl && key == 87) {  // W
        closeFile();
        return;
    }

    // Cmd+Tab or Ctrl+Tab: Next tab
    if (cmdOrCtrl && key == 258) {  // Tab key
        if (shift) {
            prevTab();
        } else {
            nextTab();
        }
        return;
    }

    FileBuffer* buf = m_impl->active();
    if (!buf) return;

    // Save: Cmd+S or Ctrl+S
    if (cmdOrCtrl && key == 83) {
        saveFile();
        return;
    }

    // Copy: Cmd+C or Ctrl+C
    if (cmdOrCtrl && key == 67) {
        if (buf->hasSelection) {
            std::string text = buf->getSelectedText();
            if (m_impl->setClipboard) {
                m_impl->setClipboard(text);
            }
            m_impl->internalClipboard = text;
        }
        return;
    }

    // Cut: Cmd+X or Ctrl+X
    if (cmdOrCtrl && key == 88) {
        if (buf->hasSelection) {
            std::string text = buf->getSelectedText();
            if (m_impl->setClipboard) {
                m_impl->setClipboard(text);
            }
            m_impl->internalClipboard = text;
            buf->deleteSelection();
        }
        return;
    }

    // Paste: Cmd+V or Ctrl+V
    if (cmdOrCtrl && key == 86) {
        std::string clipText;
        if (m_impl->getClipboard) {
            clipText = m_impl->getClipboard();
        }
        if (clipText.empty()) {
            clipText = m_impl->internalClipboard;
        }

        if (!clipText.empty()) {
            if (buf->hasSelection) {
                buf->deleteSelection();
            }
            for (char c : clipText) {
                if (c == '\n') {
                    buf->ensureLine(buf->cursorLine);
                    std::string& line = buf->lines()[buf->cursorLine];
                    std::string rest = line.substr(buf->cursorCol);
                    line = line.substr(0, buf->cursorCol);
                    buf->lines().insert(buf->lines().begin() + buf->cursorLine + 1, rest);
                    buf->cursorLine++;
                    buf->cursorCol = 0;
                } else if (c != '\r') {
                    buf->ensureLine(buf->cursorLine);
                    buf->lines()[buf->cursorLine].insert(buf->cursorCol, 1, c);
                    buf->cursorCol++;
                }
            }
            buf->setDirty(true);
        }
        return;
    }

    // Select All: Cmd+A or Ctrl+A
    if (cmdOrCtrl && key == 65) {
        buf->selectAll();
        return;
    }

    // Helper for selection with arrow keys
    auto handleArrowWithSelection = [&](bool movingCursor) {
        if (shift) {
            if (!buf->hasSelection) {
                buf->startSelection();
            }
        } else {
            if (buf->hasSelection && movingCursor) {
                buf->clearSelection();
            }
        }
    };

    auto updateSelectionAfterMove = [&]() {
        if (shift && buf->hasSelection) {
            buf->updateSelection();
        }
    };

    switch (key) {
        case 257:  // Enter
            {
                if (buf->hasSelection) {
                    buf->deleteSelection();
                }
                buf->ensureLine(buf->cursorLine);
                std::string& line = buf->lines()[buf->cursorLine];
                std::string rest = line.substr(buf->cursorCol);
                line = line.substr(0, buf->cursorCol);
                buf->lines().insert(buf->lines().begin() + buf->cursorLine + 1, rest);
                buf->cursorLine++;
                buf->cursorCol = 0;
                buf->setDirty(true);
            }
            break;

        case 259:  // Backspace
            if (buf->hasSelection) {
                buf->deleteSelection();
            } else if (buf->cursorCol > 0) {
                buf->lines()[buf->cursorLine].erase(buf->cursorCol - 1, 1);
                buf->cursorCol--;
                buf->setDirty(true);
            } else if (buf->cursorLine > 0) {
                buf->cursorCol = static_cast<int>(buf->lines()[buf->cursorLine - 1].size());
                buf->lines()[buf->cursorLine - 1] += buf->lines()[buf->cursorLine];
                buf->lines().erase(buf->lines().begin() + buf->cursorLine);
                buf->cursorLine--;
                buf->setDirty(true);
            }
            break;

        case 261:  // Delete
            if (buf->hasSelection) {
                buf->deleteSelection();
            } else if (buf->cursorCol < static_cast<int>(buf->lines()[buf->cursorLine].size())) {
                buf->lines()[buf->cursorLine].erase(buf->cursorCol, 1);
                buf->setDirty(true);
            } else if (buf->cursorLine < static_cast<int>(buf->lines().size()) - 1) {
                buf->lines()[buf->cursorLine] += buf->lines()[buf->cursorLine + 1];
                buf->lines().erase(buf->lines().begin() + buf->cursorLine + 1);
                buf->setDirty(true);
            }
            break;

        case 265:  // Up
            handleArrowWithSelection(true);
            if (buf->cursorLine > 0) {
                buf->cursorLine--;
                buf->clampCursor();
            }
            updateSelectionAfterMove();
            break;

        case 264:  // Down
            handleArrowWithSelection(true);
            if (buf->cursorLine < static_cast<int>(buf->lines().size()) - 1) {
                buf->cursorLine++;
                buf->clampCursor();
            }
            updateSelectionAfterMove();
            break;

        case 263:  // Left
            handleArrowWithSelection(true);
            if (buf->cursorCol > 0) {
                buf->cursorCol--;
            } else if (buf->cursorLine > 0) {
                buf->cursorLine--;
                buf->cursorCol = static_cast<int>(buf->lines()[buf->cursorLine].size());
            }
            updateSelectionAfterMove();
            break;

        case 262:  // Right
            handleArrowWithSelection(true);
            if (buf->cursorCol < static_cast<int>(buf->lines()[buf->cursorLine].size())) {
                buf->cursorCol++;
            } else if (buf->cursorLine < static_cast<int>(buf->lines().size()) - 1) {
                buf->cursorLine++;
                buf->cursorCol = 0;
            }
            updateSelectionAfterMove();
            break;

        case 268:  // Home
            handleArrowWithSelection(true);
            buf->cursorCol = 0;
            updateSelectionAfterMove();
            break;

        case 269:  // End
            handleArrowWithSelection(true);
            buf->cursorCol = static_cast<int>(buf->lines()[buf->cursorLine].size());
            updateSelectionAfterMove();
            break;

        case 266:  // Page Up
            handleArrowWithSelection(true);
            buf->cursorLine = std::max(0, buf->cursorLine - 20);
            buf->clampCursor();
            buf->scroll(-20);
            updateSelectionAfterMove();
            break;

        case 267:  // Page Down
            handleArrowWithSelection(true);
            buf->cursorLine = std::min(static_cast<int>(buf->lines().size()) - 1, buf->cursorLine + 20);
            buf->clampCursor();
            buf->scroll(20);
            updateSelectionAfterMove();
            break;

        case 258:  // Tab (not Cmd+Tab which is handled above)
            if (!cmdOrCtrl) {
                if (buf->hasSelection) {
                    buf->deleteSelection();
                }
                buf->ensureLine(buf->cursorLine);
                buf->lines()[buf->cursorLine].insert(buf->cursorCol, "    ");
                buf->cursorCol += 4;
                buf->setDirty(true);
            }
            break;
    }

    // Keep cursor visible
    int visibleLines = 20;
    if (buf->cursorLine < buf->scrollOffset) {
        buf->scrollOffset = buf->cursorLine;
    } else if (buf->cursorLine >= buf->scrollOffset + visibleLines) {
        buf->scrollOffset = buf->cursorLine - visibleLines + 1;
    }
}

bool EditorPanel::openFile(const std::string& path) {
    if (!m_impl) return false;

    // Check if already open
    int existing = m_impl->findBuffer(path);
    if (existing >= 0) {
        setActiveTab(existing);
        return true;
    }

    // Load new file
    auto buffer = std::make_unique<FileBuffer>();
    if (!buffer->load(path)) {
        return false;
    }

    m_impl->buffers.push_back(std::move(buffer));
    m_impl->activeIndex = static_cast<int>(m_impl->buffers.size()) - 1;

    if (m_impl->onTabChange) {
        m_impl->onTabChange(path);
    }

    return true;
}

bool EditorPanel::closeFile(int index, bool force) {
    if (!m_impl) return false;

    if (index < 0) {
        index = m_impl->activeIndex;
    }

    if (index < 0 || index >= static_cast<int>(m_impl->buffers.size())) {
        return false;
    }

    // Check for unsaved changes
    if (!force && m_impl->buffers[index]->isDirty()) {
        // TODO: Show unsaved changes dialog
        // For now, just close anyway
    }

    m_impl->buffers.erase(m_impl->buffers.begin() + index);

    // Adjust active index
    if (m_impl->buffers.empty()) {
        m_impl->activeIndex = -1;
    } else if (m_impl->activeIndex >= static_cast<int>(m_impl->buffers.size())) {
        m_impl->activeIndex = static_cast<int>(m_impl->buffers.size()) - 1;
    } else if (index < m_impl->activeIndex) {
        m_impl->activeIndex--;
    }

    if (m_impl->onTabChange && m_impl->activeIndex >= 0) {
        m_impl->onTabChange(m_impl->buffers[m_impl->activeIndex]->path());
    }

    return true;
}

bool EditorPanel::closeAllFiles(bool force) {
    if (!m_impl) return false;

    while (!m_impl->buffers.empty()) {
        if (!closeFile(0, force)) {
            return false;
        }
    }
    return true;
}

bool EditorPanel::saveFile() {
    return saveFile(-1);
}

bool EditorPanel::saveFile(int index) {
    if (!m_impl) return false;

    if (index < 0) {
        index = m_impl->activeIndex;
    }

    if (index < 0 || index >= static_cast<int>(m_impl->buffers.size())) {
        return false;
    }

    FileBuffer* buf = m_impl->buffers[index].get();
    if (!buf->save()) {
        return false;
    }

    if (m_impl->onSave) {
        m_impl->onSave(buf->path());
    }

    return true;
}

int EditorPanel::tabCount() const {
    return m_impl ? static_cast<int>(m_impl->buffers.size()) : 0;
}

int EditorPanel::activeTab() const {
    return m_impl ? m_impl->activeIndex : -1;
}

void EditorPanel::setActiveTab(int index) {
    if (!m_impl) return;
    if (index < 0 || index >= static_cast<int>(m_impl->buffers.size())) return;

    m_impl->activeIndex = index;

    if (m_impl->onTabChange) {
        m_impl->onTabChange(m_impl->buffers[index]->path());
    }
}

void EditorPanel::nextTab() {
    if (!m_impl || m_impl->buffers.empty()) return;
    int next = (m_impl->activeIndex + 1) % static_cast<int>(m_impl->buffers.size());
    setActiveTab(next);
}

void EditorPanel::prevTab() {
    if (!m_impl || m_impl->buffers.empty()) return;
    int prev = m_impl->activeIndex - 1;
    if (prev < 0) prev = static_cast<int>(m_impl->buffers.size()) - 1;
    setActiveTab(prev);
}

std::string EditorPanel::tabPath(int index) const {
    if (!m_impl) return "";
    if (index < 0 || index >= static_cast<int>(m_impl->buffers.size())) return "";
    return m_impl->buffers[index]->path();
}

bool EditorPanel::hasUnsavedChanges() const {
    if (!m_impl) return false;
    for (const auto& buf : m_impl->buffers) {
        if (buf->isDirty()) return true;
    }
    return false;
}

std::vector<std::string> EditorPanel::openFiles() const {
    std::vector<std::string> paths;
    if (m_impl) {
        for (const auto& buf : m_impl->buffers) {
            paths.push_back(buf->path());
        }
    }
    return paths;
}

void EditorPanel::setError(int line, const std::string& message) {
    if (!m_impl) return;
    FileBuffer* buf = m_impl->active();
    if (buf) {
        buf->errorLine = line;
        buf->errorMessage = message;
    }
}

void EditorPanel::clearError() {
    if (!m_impl) return;
    FileBuffer* buf = m_impl->active();
    if (buf) {
        buf->errorLine = 0;
        buf->errorMessage.clear();
    }
}

void EditorPanel::setError(const std::string& path, int line, const std::string& message) {
    if (!m_impl) return;
    int index = m_impl->findBuffer(path);
    if (index >= 0) {
        m_impl->buffers[index]->errorLine = line;
        m_impl->buffers[index]->errorMessage = message;
    }
}

void EditorPanel::clearAllErrors() {
    if (!m_impl) return;
    for (auto& buf : m_impl->buffers) {
        buf->errorLine = 0;
        buf->errorMessage.clear();
    }
}

void EditorPanel::setClipboardCallbacks(ClipboardGetCallback get, ClipboardSetCallback set) {
    if (m_impl) {
        m_impl->getClipboard = std::move(get);
        m_impl->setClipboard = std::move(set);
    }
}

void EditorPanel::onFileSave(FileSaveCallback callback) {
    if (m_impl) {
        m_impl->onSave = std::move(callback);
    }
}

void EditorPanel::onTabChange(TabChangeCallback callback) {
    if (m_impl) {
        m_impl->onTabChange = std::move(callback);
    }
}

void EditorPanel::onMouseClick(float x, float y, const glm::vec4& contentBounds,
                               float lineHeight, float charWidth) {
    if (!m_focus.focused || !m_impl) return;
    FileBuffer* buf = m_impl->active();
    if (!buf) return;
    if (lineHeight <= 0 || charWidth <= 0) return;

    // Calculate gutter width
    int lineCount = static_cast<int>(buf->lines().size());
    int gutterDigits = 1;
    int temp = lineCount;
    while (temp >= 10) { gutterDigits++; temp /= 10; }
    float gutterWidth = (gutterDigits + 2) * charWidth;

    float contentX = contentBounds.x + gutterWidth + charWidth;
    float contentY = contentBounds.y;

    if (x < contentX) return;

    // Calculate line from Y position
    float relY = y - contentY;
    int clickedLine = static_cast<int>(relY / lineHeight) + buf->scrollOffset;
    clickedLine = std::max(0, std::min(clickedLine, static_cast<int>(buf->lines().size()) - 1));

    // Calculate column from X position
    float relX = x - contentX;
    int clickedCol = static_cast<int>(relX / charWidth);
    clickedCol = std::max(0, clickedCol);
    if (clickedLine >= 0 && clickedLine < static_cast<int>(buf->lines().size())) {
        clickedCol = std::min(clickedCol, static_cast<int>(buf->lines()[clickedLine].size()));
    }

    buf->clearSelection();
    buf->cursorLine = clickedLine;
    buf->cursorCol = clickedCol;
    buf->startSelection();
    m_editorDragging = true;
}

void EditorPanel::onMouseDrag(float x, float y) {
    if (!m_focus.focused || !m_editorDragging || !m_impl) return;
    FileBuffer* buf = m_impl->active();
    if (!buf) return;

    float charWidth = m_impl->lastCharWidth;
    float lineHeight = m_impl->lastLineHeight;
    glm::vec4 bounds = m_impl->lastBounds;

    if (lineHeight <= 0 || charWidth <= 0) return;

    int lineCount = static_cast<int>(buf->lines().size());
    int gutterDigits = 1;
    int temp = lineCount;
    while (temp >= 10) { gutterDigits++; temp /= 10; }
    float gutterWidth = (gutterDigits + 2) * charWidth;

    float contentX = bounds.x + gutterWidth + charWidth;
    float contentY = bounds.y;

    float relY = y - contentY;
    int clickedLine = static_cast<int>(relY / lineHeight) + buf->scrollOffset;
    clickedLine = std::max(0, std::min(clickedLine, static_cast<int>(buf->lines().size()) - 1));

    float relX = x - contentX;
    int clickedCol = static_cast<int>(std::max(0.0f, relX) / charWidth);
    if (clickedLine >= 0 && clickedLine < static_cast<int>(buf->lines().size())) {
        clickedCol = std::min(clickedCol, static_cast<int>(buf->lines()[clickedLine].size()));
    }

    buf->cursorLine = clickedLine;
    buf->cursorCol = clickedCol;
    buf->updateSelection();
}

void EditorPanel::onMouseUp() {
    m_editorDragging = false;

    if (m_impl) {
        FileBuffer* buf = m_impl->active();
        if (buf && buf->hasSelection &&
            buf->selStartLine == buf->selEndLine &&
            buf->selStartCol == buf->selEndCol) {
            buf->clearSelection();
        }
    }
}

} // namespace vivid
