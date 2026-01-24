// Editor Panel Implementation
// Basic text editor - Zep integration planned for Phase 2

#include <vivid/ide/editor_panel.h>
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

class EditorPanel::Impl {
public:
    std::string filePath;
    std::vector<std::string> lines;
    bool dirty = false;
    int cursorLine = 0;
    int cursorCol = 0;
    int errorLine = 0;
    std::string errorMessage;

    // Selection state
    bool hasSelection = false;
    int selStartLine = 0;
    int selStartCol = 0;
    int selEndLine = 0;
    int selEndCol = 0;

    // Clipboard (simple internal clipboard, can be extended to system clipboard)
    std::string clipboard;

    // Undo/redo
    struct EditAction {
        enum Type { Insert, Delete } type;
        int line, col;
        std::string text;
    };
    std::vector<EditAction> undoStack;
    std::vector<EditAction> redoStack;
    static constexpr int MAX_UNDO = 100;

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

    // Get normalized selection (start before end)
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
            // Single line selection
            return lines[startLine].substr(startCol, endCol - startCol);
        }

        // Multi-line selection
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
            // Single line deletion
            lines[startLine].erase(startCol, endCol - startCol);
        } else {
            // Multi-line deletion
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
};

EditorPanel::EditorPanel() : m_impl(std::make_unique<Impl>()) {}
EditorPanel::~EditorPanel() = default;

bool EditorPanel::init() {
    m_impl->lines.push_back("");  // Start with one empty line
    return true;
}

bool EditorPanel::openFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    m_impl->filePath = path;
    m_impl->lines.clear();
    m_impl->dirty = false;
    m_impl->cursorLine = 0;
    m_impl->cursorCol = 0;
    m_impl->undoStack.clear();
    m_impl->redoStack.clear();

    std::string line;
    while (std::getline(file, line)) {
        m_impl->lines.push_back(line);
    }

    if (m_impl->lines.empty()) {
        m_impl->lines.push_back("");
    }

    return true;
}

const std::string& EditorPanel::filePath() const {
    return m_impl->filePath;
}

bool EditorPanel::save() {
    if (m_impl->filePath.empty()) {
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

    if (m_onSave) {
        m_onSave(m_impl->filePath);
    }

    return true;
}

bool EditorPanel::isDirty() const {
    return m_impl->dirty;
}

void EditorPanel::render(OverlayCanvas& canvas, const glm::vec4& bounds, int fontIndex) {
    float x = bounds.x;
    float y = bounds.y;
    float w = bounds.z;
    float h = bounds.w;

    // Get font metrics
    float lineHeight = canvas.fontLineHeight(fontIndex);
    float charWidth = canvas.measureText("M", fontIndex);

    if (lineHeight <= 0 || charWidth <= 0) {
        lineHeight = 16.0f;
        charWidth = 8.0f;
    }

    // Colors
    glm::vec4 bgColor(0.12f, 0.12f, 0.14f, 1.0f);
    glm::vec4 gutterColor(0.15f, 0.15f, 0.17f, 1.0f);
    glm::vec4 lineNumColor(0.5f, 0.5f, 0.5f, 1.0f);
    glm::vec4 textColor(0.9f, 0.9f, 0.9f, 1.0f);
    glm::vec4 keywordColor(0.6f, 0.8f, 1.0f, 1.0f);
    glm::vec4 commentColor(0.5f, 0.6f, 0.5f, 1.0f);
    glm::vec4 stringColor(0.8f, 0.6f, 0.5f, 1.0f);
    glm::vec4 numberColor(0.8f, 0.8f, 0.5f, 1.0f);
    glm::vec4 errorLineColor(0.5f, 0.2f, 0.2f, 0.5f);
    glm::vec4 cursorLineColor(0.2f, 0.2f, 0.25f, 1.0f);
    glm::vec4 selectionColor(0.3f, 0.4f, 0.6f, 0.5f);

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
    canvas.fillRect(x, y, w, h, bgColor);
    canvas.fillRect(x, y, gutterWidth, h, gutterColor);

    // Clip content area
    canvas.beginClipRect(x, y, w, h);

    // Calculate visible lines
    int visibleLines = static_cast<int>(h / lineHeight) + 1;
    int startLine = m_scrollOffset;
    int endLine = std::min(startLine + visibleLines, lineCount);

    bool isWgsl = m_impl->filePath.find(".wgsl") != std::string::npos;

    for (int i = startLine; i < endLine; i++) {
        float lineY = y + (i - startLine) * lineHeight;

        // Current line highlight (only if no selection)
        if (i == m_impl->cursorLine && m_focused && !m_impl->hasSelection) {
            canvas.fillRect(x + gutterWidth, lineY, w - gutterWidth, lineHeight, cursorLineColor);
        }

        // Error line highlight
        if (i + 1 == m_impl->errorLine) {
            canvas.fillRect(x + gutterWidth, lineY, w - gutterWidth, lineHeight, errorLineColor);
        }

        // Selection highlight
        if (m_impl->hasSelection && i >= selStartLine && i <= selEndLine) {
            float textX = x + gutterWidth + charWidth;
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
        canvas.text(lineNum, x + charWidth, lineY + lineHeight - 4, lineNumColor, fontIndex);

        // Line text with basic syntax highlighting
        const std::string& line = m_impl->lines[i];
        float textX = x + gutterWidth + charWidth;

        bool inComment = false;
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
            if (!inComment && (c == '"' || c == '\'')) {
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
        float cursorX = x + gutterWidth + charWidth + m_impl->cursorCol * charWidth;
        float cursorY = y + (m_impl->cursorLine - startLine) * lineHeight;
        canvas.fillRect(cursorX, cursorY, 2, lineHeight, glm::vec4(0.9f, 0.9f, 0.9f, 0.9f));
    }

    canvas.endClipRect();

    // Error message at bottom
    if (!m_impl->errorMessage.empty()) {
        float errorY = y + h - lineHeight - 4;
        canvas.fillRect(x, errorY, w, lineHeight + 4, glm::vec4(0.3f, 0.1f, 0.1f, 0.9f));
        canvas.text(m_impl->errorMessage, x + 8, errorY + lineHeight - 2, glm::vec4(1, 0.5f, 0.5f, 1), fontIndex);
    }
}

bool EditorPanel::handleInput(const FrameInput& input) {
    if (!m_focused) return false;

    // Handle scroll
    if (input.scroll.y != 0) {
        scroll(static_cast<int>(-input.scroll.y * 3));
        return true;
    }

    return false;
}

void EditorPanel::onChar(uint32_t codepoint) {
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

    bool ctrl = (mods & 0x2) != 0;   // GLFW_MOD_CONTROL
    bool shift = (mods & 0x1) != 0;  // GLFW_MOD_SHIFT
    bool super = (mods & 0x8) != 0;  // GLFW_MOD_SUPER (Cmd on macOS)
    bool cmdOrCtrl = ctrl || super;

    // Save: Cmd+S or Ctrl+S
    if (cmdOrCtrl && key == 83) {  // S
        save();
        return;
    }

    // Copy: Cmd+C or Ctrl+C
    if (cmdOrCtrl && key == 67) {  // C
        if (m_impl->hasSelection) {
            std::string text = m_impl->getSelectedText();
            if (m_setClipboard) {
                m_setClipboard(text);
            }
            m_impl->clipboard = text;  // Also store internally as fallback
        }
        return;
    }

    // Cut: Cmd+X or Ctrl+X
    if (cmdOrCtrl && key == 88) {  // X
        if (m_impl->hasSelection) {
            std::string text = m_impl->getSelectedText();
            if (m_setClipboard) {
                m_setClipboard(text);
            }
            m_impl->clipboard = text;  // Also store internally as fallback
            m_impl->deleteSelection();
        }
        return;
    }

    // Paste: Cmd+V or Ctrl+V
    if (cmdOrCtrl && key == 86) {  // V
        std::string clipText;
        if (m_getClipboard) {
            clipText = m_getClipboard();
        }
        if (clipText.empty()) {
            clipText = m_impl->clipboard;  // Fallback to internal
        }

        if (!clipText.empty()) {
            // Delete selection if any
            if (m_impl->hasSelection) {
                m_impl->deleteSelection();
            }
            // Insert clipboard text
            for (char c : clipText) {
                if (c == '\n') {
                    // Split line at cursor
                    m_impl->ensureLine(m_impl->cursorLine);
                    std::string& line = m_impl->lines[m_impl->cursorLine];
                    std::string rest = line.substr(m_impl->cursorCol);
                    line = line.substr(0, m_impl->cursorCol);
                    m_impl->lines.insert(m_impl->lines.begin() + m_impl->cursorLine + 1, rest);
                    m_impl->cursorLine++;
                    m_impl->cursorCol = 0;
                } else if (c != '\r') {  // Skip carriage returns
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
    if (cmdOrCtrl && key == 65) {  // A
        m_impl->selectAll();
        return;
    }

    // Undo: Cmd+Z or Ctrl+Z
    if (cmdOrCtrl && key == 90) {  // Z
        // TODO: implement undo
        return;
    }

    // Helper to handle selection with arrow keys
    auto handleArrowWithSelection = [&](bool movingCursor) {
        if (shift) {
            // Extend/start selection
            if (!m_impl->hasSelection) {
                m_impl->startSelection();
            }
        } else {
            // Clear selection on arrow key without shift
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
                // Merge with previous line
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
                // Merge with next line
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
            scroll(-20);
            updateSelectionAfterMove();
            break;

        case 267:  // Page Down
            handleArrowWithSelection(true);
            m_impl->cursorLine = std::min(static_cast<int>(m_impl->lines.size()) - 1, m_impl->cursorLine + 20);
            m_impl->clampCursor();
            scroll(20);
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
    int visibleLines = 20;  // Approximate
    if (m_impl->cursorLine < m_scrollOffset) {
        m_scrollOffset = m_impl->cursorLine;
    } else if (m_impl->cursorLine >= m_scrollOffset + visibleLines) {
        m_scrollOffset = m_impl->cursorLine - visibleLines + 1;
    }
}

void EditorPanel::setError(int line, const std::string& message) {
    m_impl->errorLine = line;
    m_impl->errorMessage = message;
}

void EditorPanel::clearError() {
    m_impl->errorLine = 0;
    m_impl->errorMessage.clear();
}

void EditorPanel::onSave(std::function<void(const std::string&)> callback) {
    m_onSave = std::move(callback);
}

int EditorPanel::cursorLine() const { return m_impl->cursorLine; }
int EditorPanel::cursorColumn() const { return m_impl->cursorCol; }

void EditorPanel::gotoLine(int line) {
    m_impl->cursorLine = std::max(0, std::min(line - 1, static_cast<int>(m_impl->lines.size()) - 1));
    m_impl->cursorCol = 0;
    m_scrollOffset = std::max(0, m_impl->cursorLine - 10);
}

int EditorPanel::lineCount() const {
    return static_cast<int>(m_impl->lines.size());
}

void EditorPanel::scroll(int delta) {
    m_scrollOffset = std::max(0, m_scrollOffset + delta);
    int maxScroll = std::max(0, static_cast<int>(m_impl->lines.size()) - 10);
    m_scrollOffset = std::min(m_scrollOffset, maxScroll);
}

void EditorPanel::onMouseClick(float x, float y, const glm::vec4& bounds, float fontLineHeight, float charWidth) {
    if (!m_focused) return;
    if (fontLineHeight <= 0 || charWidth <= 0) return;

    // Store metrics for drag operations
    m_lastBounds = bounds;
    m_lastLineHeight = fontLineHeight;
    m_lastCharWidth = charWidth;

    // Calculate gutter width (same as in render())
    int lineCount = static_cast<int>(m_impl->lines.size());
    int gutterDigits = 1;
    int temp = lineCount;
    while (temp >= 10) { gutterDigits++; temp /= 10; }
    float gutterWidth = (gutterDigits + 2) * charWidth;

    // Content area starts after gutter
    float contentX = bounds.x + gutterWidth + charWidth;
    float contentY = bounds.y;

    // Check if click is in the content area (not in gutter)
    if (x < contentX) return;

    // Calculate line from Y position
    float relY = y - contentY;
    int clickedLine = static_cast<int>(relY / fontLineHeight) + m_scrollOffset;

    // Clamp to valid line range
    clickedLine = std::max(0, std::min(clickedLine, static_cast<int>(m_impl->lines.size()) - 1));

    // Calculate column from X position
    float relX = x - contentX;
    int clickedCol = static_cast<int>(relX / charWidth);

    // Clamp to valid column range for this line
    clickedCol = std::max(0, clickedCol);
    if (clickedLine >= 0 && clickedLine < static_cast<int>(m_impl->lines.size())) {
        clickedCol = std::min(clickedCol, static_cast<int>(m_impl->lines[clickedLine].size()));
    }

    // Clear any existing selection and start new one
    m_impl->clearSelection();

    // Update cursor position
    m_impl->cursorLine = clickedLine;
    m_impl->cursorCol = clickedCol;

    // Start selection (will be extended by drag)
    m_impl->startSelection();
    m_isDragging = true;
}

void EditorPanel::onMouseDrag(float x, float y) {
    if (!m_focused || !m_isDragging) return;
    if (m_lastLineHeight <= 0 || m_lastCharWidth <= 0) return;

    // Use stored metrics from click
    float charWidth = m_lastCharWidth;
    float fontLineHeight = m_lastLineHeight;
    glm::vec4 bounds = m_lastBounds;

    // Calculate gutter width
    int lineCount = static_cast<int>(m_impl->lines.size());
    int gutterDigits = 1;
    int temp = lineCount;
    while (temp >= 10) { gutterDigits++; temp /= 10; }
    float gutterWidth = (gutterDigits + 2) * charWidth;

    float contentX = bounds.x + gutterWidth + charWidth;
    float contentY = bounds.y;

    // Calculate line from Y position
    float relY = y - contentY;
    int clickedLine = static_cast<int>(relY / fontLineHeight) + m_scrollOffset;
    clickedLine = std::max(0, std::min(clickedLine, static_cast<int>(m_impl->lines.size()) - 1));

    // Calculate column from X position
    float relX = x - contentX;
    int clickedCol = static_cast<int>(std::max(0.0f, relX) / charWidth);
    if (clickedLine >= 0 && clickedLine < static_cast<int>(m_impl->lines.size())) {
        clickedCol = std::min(clickedCol, static_cast<int>(m_impl->lines[clickedLine].size()));
    }

    // Update cursor and selection end
    m_impl->cursorLine = clickedLine;
    m_impl->cursorCol = clickedCol;
    m_impl->updateSelection();
}

void EditorPanel::onMouseUp() {
    m_isDragging = false;

    // If selection start equals end, clear selection (just a click, no drag)
    if (m_impl->hasSelection &&
        m_impl->selStartLine == m_impl->selEndLine &&
        m_impl->selStartCol == m_impl->selEndCol) {
        m_impl->clearSelection();
    }
}

void EditorPanel::setClipboardCallbacks(
    std::function<std::string()> getCb,
    std::function<void(const std::string&)> setCb
) {
    m_getClipboard = std::move(getCb);
    m_setClipboard = std::move(setCb);
}

} // namespace vivid
