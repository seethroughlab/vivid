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
    bool selecting = false;
    int selStartLine = 0;
    int selStartCol = 0;

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

        // Current line highlight
        if (i == m_impl->cursorLine && m_focused) {
            canvas.fillRect(x + gutterWidth, lineY, w - gutterWidth, lineHeight, cursorLineColor);
        }

        // Error line highlight
        if (i + 1 == m_impl->errorLine) {
            canvas.fillRect(x + gutterWidth, lineY, w - gutterWidth, lineHeight, errorLineColor);
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
    bool super = (mods & 0x8) != 0;  // GLFW_MOD_SUPER (Cmd on macOS)
    bool cmdOrCtrl = ctrl || super;

    // Save: Cmd+S or Ctrl+S
    if (cmdOrCtrl && key == 83) {  // S
        save();
        return;
    }

    // Undo: Cmd+Z or Ctrl+Z
    if (cmdOrCtrl && key == 90) {  // Z
        // TODO: implement undo
        return;
    }

    switch (key) {
        case 257:  // Enter
            {
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
            if (m_impl->cursorCol > 0) {
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
            if (m_impl->cursorCol < static_cast<int>(m_impl->lines[m_impl->cursorLine].size())) {
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
            if (m_impl->cursorLine > 0) {
                m_impl->cursorLine--;
                m_impl->clampCursor();
            }
            break;

        case 264:  // Down
            if (m_impl->cursorLine < static_cast<int>(m_impl->lines.size()) - 1) {
                m_impl->cursorLine++;
                m_impl->clampCursor();
            }
            break;

        case 263:  // Left
            if (m_impl->cursorCol > 0) {
                m_impl->cursorCol--;
            } else if (m_impl->cursorLine > 0) {
                m_impl->cursorLine--;
                m_impl->cursorCol = static_cast<int>(m_impl->lines[m_impl->cursorLine].size());
            }
            break;

        case 262:  // Right
            if (m_impl->cursorCol < static_cast<int>(m_impl->lines[m_impl->cursorLine].size())) {
                m_impl->cursorCol++;
            } else if (m_impl->cursorLine < static_cast<int>(m_impl->lines.size()) - 1) {
                m_impl->cursorLine++;
                m_impl->cursorCol = 0;
            }
            break;

        case 268:  // Home
            m_impl->cursorCol = 0;
            break;

        case 269:  // End
            m_impl->cursorCol = static_cast<int>(m_impl->lines[m_impl->cursorLine].size());
            break;

        case 266:  // Page Up
            m_impl->cursorLine = std::max(0, m_impl->cursorLine - 20);
            m_impl->clampCursor();
            scroll(-20);
            break;

        case 267:  // Page Down
            m_impl->cursorLine = std::min(static_cast<int>(m_impl->lines.size()) - 1, m_impl->cursorLine + 20);
            m_impl->clampCursor();
            scroll(20);
            break;

        case 258:  // Tab
            {
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

} // namespace vivid
