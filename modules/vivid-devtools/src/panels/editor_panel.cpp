// Editor Panel Implementation
// Basic text editor with syntax highlighting
// Migrated from vivid-ide to vivid-devtools

#include <vivid/devtools/panels/editor_panel.h>
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
    std::string filePath;
    std::vector<std::string> lines;
    bool dirty = false;
    int cursorLine = 0;
    int cursorCol = 0;
    int errorLine = 0;
    std::string errorMessage;
    int scrollOffset = 0;
    UIStyle style;

    // Selection state
    bool hasSelection = false;
    int selStartLine = 0;
    int selStartCol = 0;
    int selEndLine = 0;
    int selEndCol = 0;

    // Clipboard callbacks
    ClipboardGetCallback getClipboard;
    ClipboardSetCallback setClipboard;
    std::string internalClipboard;  // Fallback

    // Mouse drag state
    bool isDragging = false;
    glm::vec4 lastBounds = {0, 0, 0, 0};
    float lastLineHeight = 0;
    float lastCharWidth = 0;

    // Save callback
    std::function<void(const std::string&)> onSave;

    void ensureLine(int line) {
        while (lines.size() <= static_cast<size_t>(line)) {
            lines.push_back("");
        }
    }

    void clampCursor() {
        cursorLine = std::max(0, std::min(cursorLine, static_cast<int>(lines.size()) - 1));
        if (cursorLine >= 0 && cursorLine < static_cast<int>(lines.size())) {
            cursorCol = std::max(0, std::min(cursorCol, static_cast<int>(lines[cursorLine].size())));
        } else {
            cursorCol = 0;
        }
    }

    void clearSelection() {
        hasSelection = false;
    }

    void startSelection() {
        selStartLine = cursorLine;
        selStartCol = cursorCol;
        selEndLine = cursorLine;
        selEndCol = cursorCol;
        hasSelection = true;
    }

    void updateSelection() {
        selEndLine = cursorLine;
        selEndCol = cursorCol;
    }

    void getNormalizedSelection(int& startLine, int& startCol, int& endLine, int& endCol) const {
        if (selStartLine < selEndLine || (selStartLine == selEndLine && selStartCol <= selEndCol)) {
            startLine = selStartLine;
            startCol = selStartCol;
            endLine = selEndLine;
            endCol = selEndCol;
        } else {
            startLine = selEndLine;
            startCol = selEndCol;
            endLine = selStartLine;
            endCol = selStartCol;
        }
    }

    std::string getSelectedText() const {
        if (!hasSelection) return "";

        int startLine, startCol, endLine, endCol;
        getNormalizedSelection(startLine, startCol, endLine, endCol);

        if (startLine == endLine) {
            return lines[startLine].substr(startCol, endCol - startCol);
        }

        std::string result;
        result += lines[startLine].substr(startCol);
        result += '\n';
        for (int i = startLine + 1; i < endLine; i++) {
            result += lines[i];
            result += '\n';
        }
        result += lines[endLine].substr(0, endCol);
        return result;
    }

    void deleteSelection() {
        if (!hasSelection) return;

        int startLine, startCol, endLine, endCol;
        getNormalizedSelection(startLine, startCol, endLine, endCol);

        if (startLine == endLine) {
            lines[startLine].erase(startCol, endCol - startCol);
        } else {
            std::string newLine = lines[startLine].substr(0, startCol) + lines[endLine].substr(endCol);
            lines[startLine] = newLine;
            lines.erase(lines.begin() + startLine + 1, lines.begin() + endLine + 1);
        }

        cursorLine = startLine;
        cursorCol = startCol;
        hasSelection = false;
        dirty = true;
    }

    void selectAll() {
        if (lines.empty()) return;
        selStartLine = 0;
        selStartCol = 0;
        selEndLine = static_cast<int>(lines.size()) - 1;
        selEndCol = static_cast<int>(lines[selEndLine].size());
        hasSelection = true;
        cursorLine = selEndLine;
        cursorCol = selEndCol;
    }

    bool isKeyword(const std::string& word, bool wgsl) {
        const auto& keywords = wgsl ? WGSL_KEYWORDS : CPP_KEYWORDS;
        return std::find(keywords.begin(), keywords.end(), word) != keywords.end();
    }

    void scroll(int delta) {
        scrollOffset = std::max(0, scrollOffset + delta);
        int maxScroll = std::max(0, static_cast<int>(lines.size()) - 10);
        scrollOffset = std::min(scrollOffset, maxScroll);
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
    m_impl->lines.push_back("");  // Start with one empty line
    return true;
}

void EditorPanel::shutdown() {
    m_impl.reset();
}

void EditorPanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                         const FrameInput& input, const UIStyle& style) {
    if (!m_config.visible || !m_impl) {
        m_consumedInput = false;
        m_hovered = false;
        return;
    }

    // Store style for use throughout render
    m_impl->style = style;

    // Handle drag/resize (in logical coordinates)
    // Compute logical screen dimensions for proper clamping on HiDPI displays
    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;
    float screenW = static_cast<float>(input.width) / scale;
    float screenH = static_cast<float>(input.height) / scale;
    handleDragAndResize(input, screenW, screenH);

    // Get bounds (already in logical coordinates)
    glm::vec4 scaledBounds = m_config.bounds;

    float x = scaledBounds.x;
    float y = scaledBounds.y;
    float w = scaledBounds.z;
    float h = scaledBounds.w;

    // Panel chrome
    float titleBarHeight = style.titleBarHeight();
    float contentY = y + titleBarHeight;
    float contentH = h - titleBarHeight;

    // Render chrome
    renderChrome(canvas, x, y, w, h, style);

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
    if (m_impl->hasSelection) {
        m_impl->getNormalizedSelection(selStartLine, selStartCol, selEndLine, selEndCol);
    }

    // Line number gutter width
    int lineCount = static_cast<int>(m_impl->lines.size());
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
    int startLine = m_impl->scrollOffset;
    int endLine = std::min(startLine + visibleLines, lineCount);

    bool isWgsl = m_impl->filePath.find(".wgsl") != std::string::npos;

    for (int i = startLine; i < endLine; i++) {
        float lineY = contentY + (i - startLine) * lineHeight;

        // Current line highlight (only if no selection)
        if (i == m_impl->cursorLine && m_focused && !m_impl->hasSelection) {
            canvas.fillRect(contentX + gutterWidth, lineY, contentW - gutterWidth, lineHeight, cursorLineColor);
        }

        // Error line highlight
        if (i + 1 == m_impl->errorLine) {
            canvas.fillRect(contentX + gutterWidth, lineY, contentW - gutterWidth, lineHeight, errorLineColor);
        }

        // Selection highlight
        if (m_impl->hasSelection && i >= selStartLine && i <= selEndLine) {
            float textX = contentX + gutterWidth + charWidth;
            int lineLen = static_cast<int>(m_impl->lines[i].size());

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
        const std::string& line = m_impl->lines[i];
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
    if (m_focused && m_impl->cursorLine >= startLine && m_impl->cursorLine < endLine) {
        float cursorX = contentX + gutterWidth + charWidth + m_impl->cursorCol * charWidth;
        float cursorY = contentY + (m_impl->cursorLine - startLine) * lineHeight;
        canvas.fillRect(cursorX, cursorY, 2, lineHeight, style.terminalCursor);
    }

    canvas.endClipRect();

    // Error message at bottom
    if (!m_impl->errorMessage.empty()) {
        float errorY = y + h - lineHeight - 4;
        canvas.fillRect(x, errorY, w, lineHeight + 4, style.editorErrorLine);
        canvas.text(m_impl->errorMessage, x + 8, errorY + lineHeight - 2, style.error, fontIndex);
    }
}

bool EditorPanel::handleInput(const FrameInput& input) {
    if (!m_focused || !m_impl) return false;

    // Handle scroll
    if (input.scroll.y != 0) {
        m_impl->scroll(static_cast<int>(-input.scroll.y * 3));
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
        // Start selection on click
        onMouseClick(input.mousePos.x, input.mousePos.y, contentBounds, lineHeight, charWidth);
        return true;
    } else if (m_dragging && input.mouseDown[0]) {
        // Continue selection while dragging
        onMouseDrag(input.mousePos.x, input.mousePos.y);
        return true;
    } else if (m_dragging && !input.mouseDown[0]) {
        // End selection on release
        onMouseUp();
        return true;
    }

    return false;
}

void EditorPanel::onChar(uint32_t codepoint) {
    if (!m_impl) return;
    if (codepoint < 32 || codepoint > 126) return;

    // Delete selection if any
    if (m_impl->hasSelection) {
        m_impl->deleteSelection();
    }

    m_impl->ensureLine(m_impl->cursorLine);
    std::string& line = m_impl->lines[m_impl->cursorLine];

    char c = static_cast<char>(codepoint);
    line.insert(m_impl->cursorCol, 1, c);
    m_impl->cursorCol++;
    m_impl->dirty = true;
    m_impl->clampCursor();
}

void EditorPanel::onKeyDown(int key, int mods) {
    if (!m_impl) return;

    bool ctrl = (mods & 0x2) != 0;   // GLFW_MOD_CONTROL
    bool shift = (mods & 0x1) != 0;  // GLFW_MOD_SHIFT
    bool super = (mods & 0x8) != 0;  // GLFW_MOD_SUPER (Cmd on macOS)
    bool cmdOrCtrl = ctrl || super;

    // Save: Cmd+S or Ctrl+S
    if (cmdOrCtrl && key == 83) {
        saveFile();
        return;
    }

    // Copy: Cmd+C or Ctrl+C
    if (cmdOrCtrl && key == 67) {
        if (m_impl->hasSelection) {
            std::string text = m_impl->getSelectedText();
            if (m_impl->setClipboard) {
                m_impl->setClipboard(text);
            }
            m_impl->internalClipboard = text;
        }
        return;
    }

    // Cut: Cmd+X or Ctrl+X
    if (cmdOrCtrl && key == 88) {
        if (m_impl->hasSelection) {
            std::string text = m_impl->getSelectedText();
            if (m_impl->setClipboard) {
                m_impl->setClipboard(text);
            }
            m_impl->internalClipboard = text;
            m_impl->deleteSelection();
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
            if (m_impl->hasSelection) {
                m_impl->deleteSelection();
            }
            for (char c : clipText) {
                if (c == '\n') {
                    m_impl->ensureLine(m_impl->cursorLine);
                    std::string& line = m_impl->lines[m_impl->cursorLine];
                    std::string rest = line.substr(m_impl->cursorCol);
                    line = line.substr(0, m_impl->cursorCol);
                    m_impl->lines.insert(m_impl->lines.begin() + m_impl->cursorLine + 1, rest);
                    m_impl->cursorLine++;
                    m_impl->cursorCol = 0;
                } else if (c != '\r') {
                    m_impl->ensureLine(m_impl->cursorLine);
                    m_impl->lines[m_impl->cursorLine].insert(m_impl->cursorCol, 1, c);
                    m_impl->cursorCol++;
                }
            }
            m_impl->dirty = true;
        }
        return;
    }

    // Select All: Cmd+A or Ctrl+A
    if (cmdOrCtrl && key == 65) {
        m_impl->selectAll();
        return;
    }

    // Helper for selection with arrow keys
    auto handleArrowWithSelection = [&](bool movingCursor) {
        if (shift) {
            if (!m_impl->hasSelection) {
                m_impl->startSelection();
            }
        } else {
            if (m_impl->hasSelection && movingCursor) {
                m_impl->clearSelection();
            }
        }
    };

    auto updateSelectionAfterMove = [&]() {
        if (shift && m_impl->hasSelection) {
            m_impl->updateSelection();
        }
    };

    switch (key) {
        case 257:  // Enter
            {
                if (m_impl->hasSelection) {
                    m_impl->deleteSelection();
                }
                m_impl->ensureLine(m_impl->cursorLine);
                std::string& line = m_impl->lines[m_impl->cursorLine];
                std::string rest = line.substr(m_impl->cursorCol);
                line = line.substr(0, m_impl->cursorCol);
                m_impl->lines.insert(m_impl->lines.begin() + m_impl->cursorLine + 1, rest);
                m_impl->cursorLine++;
                m_impl->cursorCol = 0;
                m_impl->dirty = true;
            }
            break;

        case 259:  // Backspace
            if (m_impl->hasSelection) {
                m_impl->deleteSelection();
            } else if (m_impl->cursorCol > 0) {
                m_impl->lines[m_impl->cursorLine].erase(m_impl->cursorCol - 1, 1);
                m_impl->cursorCol--;
                m_impl->dirty = true;
            } else if (m_impl->cursorLine > 0) {
                m_impl->cursorCol = static_cast<int>(m_impl->lines[m_impl->cursorLine - 1].size());
                m_impl->lines[m_impl->cursorLine - 1] += m_impl->lines[m_impl->cursorLine];
                m_impl->lines.erase(m_impl->lines.begin() + m_impl->cursorLine);
                m_impl->cursorLine--;
                m_impl->dirty = true;
            }
            break;

        case 261:  // Delete
            if (m_impl->hasSelection) {
                m_impl->deleteSelection();
            } else if (m_impl->cursorCol < static_cast<int>(m_impl->lines[m_impl->cursorLine].size())) {
                m_impl->lines[m_impl->cursorLine].erase(m_impl->cursorCol, 1);
                m_impl->dirty = true;
            } else if (m_impl->cursorLine < static_cast<int>(m_impl->lines.size()) - 1) {
                m_impl->lines[m_impl->cursorLine] += m_impl->lines[m_impl->cursorLine + 1];
                m_impl->lines.erase(m_impl->lines.begin() + m_impl->cursorLine + 1);
                m_impl->dirty = true;
            }
            break;

        case 265:  // Up
            handleArrowWithSelection(true);
            if (m_impl->cursorLine > 0) {
                m_impl->cursorLine--;
                m_impl->clampCursor();
            }
            updateSelectionAfterMove();
            break;

        case 264:  // Down
            handleArrowWithSelection(true);
            if (m_impl->cursorLine < static_cast<int>(m_impl->lines.size()) - 1) {
                m_impl->cursorLine++;
                m_impl->clampCursor();
            }
            updateSelectionAfterMove();
            break;

        case 263:  // Left
            handleArrowWithSelection(true);
            if (m_impl->cursorCol > 0) {
                m_impl->cursorCol--;
            } else if (m_impl->cursorLine > 0) {
                m_impl->cursorLine--;
                m_impl->cursorCol = static_cast<int>(m_impl->lines[m_impl->cursorLine].size());
            }
            updateSelectionAfterMove();
            break;

        case 262:  // Right
            handleArrowWithSelection(true);
            if (m_impl->cursorCol < static_cast<int>(m_impl->lines[m_impl->cursorLine].size())) {
                m_impl->cursorCol++;
            } else if (m_impl->cursorLine < static_cast<int>(m_impl->lines.size()) - 1) {
                m_impl->cursorLine++;
                m_impl->cursorCol = 0;
            }
            updateSelectionAfterMove();
            break;

        case 268:  // Home
            handleArrowWithSelection(true);
            m_impl->cursorCol = 0;
            updateSelectionAfterMove();
            break;

        case 269:  // End
            handleArrowWithSelection(true);
            m_impl->cursorCol = static_cast<int>(m_impl->lines[m_impl->cursorLine].size());
            updateSelectionAfterMove();
            break;

        case 266:  // Page Up
            handleArrowWithSelection(true);
            m_impl->cursorLine = std::max(0, m_impl->cursorLine - 20);
            m_impl->clampCursor();
            m_impl->scroll(-20);
            updateSelectionAfterMove();
            break;

        case 267:  // Page Down
            handleArrowWithSelection(true);
            m_impl->cursorLine = std::min(static_cast<int>(m_impl->lines.size()) - 1, m_impl->cursorLine + 20);
            m_impl->clampCursor();
            m_impl->scroll(20);
            updateSelectionAfterMove();
            break;

        case 258:  // Tab
            {
                if (m_impl->hasSelection) {
                    m_impl->deleteSelection();
                }
                m_impl->ensureLine(m_impl->cursorLine);
                m_impl->lines[m_impl->cursorLine].insert(m_impl->cursorCol, "    ");
                m_impl->cursorCol += 4;
                m_impl->dirty = true;
            }
            break;
    }

    // Keep cursor visible
    int visibleLines = 20;
    if (m_impl->cursorLine < m_impl->scrollOffset) {
        m_impl->scrollOffset = m_impl->cursorLine;
    } else if (m_impl->cursorLine >= m_impl->scrollOffset + visibleLines) {
        m_impl->scrollOffset = m_impl->cursorLine - visibleLines + 1;
    }
}

bool EditorPanel::openFile(const std::string& path) {
    if (!m_impl) return false;

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    m_impl->filePath = path;
    m_impl->lines.clear();
    m_impl->dirty = false;
    m_impl->cursorLine = 0;
    m_impl->cursorCol = 0;
    m_impl->scrollOffset = 0;
    m_impl->clearSelection();

    std::string line;
    while (std::getline(file, line)) {
        m_impl->lines.push_back(line);
    }

    if (m_impl->lines.empty()) {
        m_impl->lines.push_back("");
    }

    return true;
}

bool EditorPanel::saveFile() {
    if (!m_impl || m_impl->filePath.empty()) {
        return false;
    }

    std::ofstream file(m_impl->filePath);
    if (!file.is_open()) {
        return false;
    }

    for (size_t i = 0; i < m_impl->lines.size(); i++) {
        file << m_impl->lines[i];
        if (i < m_impl->lines.size() - 1) {
            file << '\n';
        }
    }

    m_impl->dirty = false;

    if (m_impl->onSave) {
        m_impl->onSave(m_impl->filePath);
    }

    return true;
}

void EditorPanel::setError(int line, const std::string& message) {
    if (!m_impl) return;
    m_impl->errorLine = line;
    m_impl->errorMessage = message;
}

void EditorPanel::clearError() {
    if (!m_impl) return;
    m_impl->errorLine = 0;
    m_impl->errorMessage.clear();
}

void EditorPanel::setClipboardCallbacks(ClipboardGetCallback get, ClipboardSetCallback set) {
    if (m_impl) {
        m_impl->getClipboard = std::move(get);
        m_impl->setClipboard = std::move(set);
    }
}

void EditorPanel::onMouseClick(float x, float y, const glm::vec4& contentBounds,
                               float lineHeight, float charWidth) {
    if (!m_focused || !m_impl) return;
    if (lineHeight <= 0 || charWidth <= 0) return;

    // Calculate gutter width
    int lineCount = static_cast<int>(m_impl->lines.size());
    int gutterDigits = 1;
    int temp = lineCount;
    while (temp >= 10) { gutterDigits++; temp /= 10; }
    float gutterWidth = (gutterDigits + 2) * charWidth;

    float contentX = contentBounds.x + gutterWidth + charWidth;
    float contentY = contentBounds.y;

    if (x < contentX) return;

    // Calculate line from Y position
    float relY = y - contentY;
    int clickedLine = static_cast<int>(relY / lineHeight) + m_impl->scrollOffset;
    clickedLine = std::max(0, std::min(clickedLine, static_cast<int>(m_impl->lines.size()) - 1));

    // Calculate column from X position
    float relX = x - contentX;
    int clickedCol = static_cast<int>(relX / charWidth);
    clickedCol = std::max(0, clickedCol);
    if (clickedLine >= 0 && clickedLine < static_cast<int>(m_impl->lines.size())) {
        clickedCol = std::min(clickedCol, static_cast<int>(m_impl->lines[clickedLine].size()));
    }

    m_impl->clearSelection();
    m_impl->cursorLine = clickedLine;
    m_impl->cursorCol = clickedCol;
    m_impl->startSelection();
    m_dragging = true;
}

void EditorPanel::onMouseDrag(float x, float y) {
    if (!m_focused || !m_dragging || !m_impl) return;

    float charWidth = m_impl->lastCharWidth;
    float lineHeight = m_impl->lastLineHeight;
    glm::vec4 bounds = m_impl->lastBounds;

    if (lineHeight <= 0 || charWidth <= 0) return;

    int lineCount = static_cast<int>(m_impl->lines.size());
    int gutterDigits = 1;
    int temp = lineCount;
    while (temp >= 10) { gutterDigits++; temp /= 10; }
    float gutterWidth = (gutterDigits + 2) * charWidth;

    float contentX = bounds.x + gutterWidth + charWidth;
    float contentY = bounds.y;

    float relY = y - contentY;
    int clickedLine = static_cast<int>(relY / lineHeight) + m_impl->scrollOffset;
    clickedLine = std::max(0, std::min(clickedLine, static_cast<int>(m_impl->lines.size()) - 1));

    float relX = x - contentX;
    int clickedCol = static_cast<int>(std::max(0.0f, relX) / charWidth);
    if (clickedLine >= 0 && clickedLine < static_cast<int>(m_impl->lines.size())) {
        clickedCol = std::min(clickedCol, static_cast<int>(m_impl->lines[clickedLine].size()));
    }

    m_impl->cursorLine = clickedLine;
    m_impl->cursorCol = clickedCol;
    m_impl->updateSelection();
}

void EditorPanel::onMouseUp() {
    m_dragging = false;

    if (m_impl && m_impl->hasSelection &&
        m_impl->selStartLine == m_impl->selEndLine &&
        m_impl->selStartCol == m_impl->selEndCol) {
        m_impl->clearSelection();
    }
}

} // namespace vivid
