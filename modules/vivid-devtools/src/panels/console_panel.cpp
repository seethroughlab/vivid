// Console Panel Implementation
// Displays compile errors and messages with selection/copy support

#include <vivid/devtools/panels/console_panel.h>
#include <vivid/context.h>
#include <vivid/gui/ui_style.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace vivid {

// Maximum number of messages to keep in history
static constexpr size_t MAX_MESSAGES = 1000;

// Console message structure
struct ConsoleMessage {
    ConsoleMessageType type;
    std::string text;
    std::string file;       // For compile errors
    int line = 0;           // For compile errors
    std::string timestamp;  // HH:MM:SS format

    // Line indices for selection (set during layout)
    int startLineIndex = 0;
    int lineCount = 1;
};

struct ConsolePanel::Impl {
    UIStyle style;
    std::vector<ConsoleMessage> messages;

    // Scroll state
    float scrollOffset = 0.0f;
    float maxScroll = 0.0f;
    float contentHeight = 0.0f;

    // Selection state
    bool selecting = false;
    int selectionStartLine = -1;
    int selectionStartCol = -1;
    int selectionEndLine = -1;
    int selectionEndCol = -1;
    bool hasSelection = false;

    // Clipboard callbacks
    std::function<std::string()> getClipboard;
    std::function<void(const std::string&)> setClipboard;

    // Layout cache
    std::vector<std::string> wrappedLines;  // All lines for rendering
    std::vector<size_t> lineToMsgIndex;     // Maps line index to message index
    float lineHeight = 16.0f;
    float charWidth = 8.0f;
    int charsPerLine = 80;

    static std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time);
#else
        localtime_r(&time, &tm_buf);
#endif
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << tm_buf.tm_hour << ":"
            << std::setfill('0') << std::setw(2) << tm_buf.tm_min << ":"
            << std::setfill('0') << std::setw(2) << tm_buf.tm_sec;
        return oss.str();
    }

    glm::vec4 getColorForType(ConsoleMessageType type) const {
        switch (type) {
            case ConsoleMessageType::Info:
                return style.textPrimary;
            case ConsoleMessageType::Warning:
                return style.warning;
            case ConsoleMessageType::Error:
            case ConsoleMessageType::CompileError:
                return style.error;
            case ConsoleMessageType::Debug:
                return style.textDim;
            default:
                return style.textPrimary;
        }
    }

    void rebuildWrappedLines() {
        wrappedLines.clear();
        lineToMsgIndex.clear();

        for (size_t msgIdx = 0; msgIdx < messages.size(); ++msgIdx) {
            const auto& msg = messages[msgIdx];

            // Build the full line text
            std::string lineText;

            // Add timestamp
            lineText = "[" + msg.timestamp + "] ";

            // Add file:line for compile errors
            if (msg.type == ConsoleMessageType::CompileError && !msg.file.empty()) {
                // Extract just the filename
                std::string filename = msg.file;
                size_t lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos) {
                    filename = filename.substr(lastSlash + 1);
                }
                lineText += filename + ":" + std::to_string(msg.line) + ": ";
            }

            // Add prefix for non-compile messages
            switch (msg.type) {
                case ConsoleMessageType::Warning:
                    lineText += "warning: ";
                    break;
                case ConsoleMessageType::Error:
                    lineText += "error: ";
                    break;
                case ConsoleMessageType::Debug:
                    lineText += "debug: ";
                    break;
                default:
                    break;
            }

            lineText += msg.text;

            // Word wrap the line
            if (charsPerLine > 0 && lineText.length() > static_cast<size_t>(charsPerLine)) {
                // Simple wrapping - break at character boundary
                for (size_t i = 0; i < lineText.length(); i += charsPerLine) {
                    size_t len = std::min(static_cast<size_t>(charsPerLine), lineText.length() - i);
                    wrappedLines.push_back(lineText.substr(i, len));
                    lineToMsgIndex.push_back(msgIdx);
                }
            } else {
                wrappedLines.push_back(lineText);
                lineToMsgIndex.push_back(msgIdx);
            }
        }
    }

    std::string getSelectedText() const {
        if (!hasSelection || selectionStartLine < 0 || selectionEndLine < 0) {
            return "";
        }

        int startLine = std::min(selectionStartLine, selectionEndLine);
        int endLine = std::max(selectionStartLine, selectionEndLine);
        int startCol = (selectionStartLine <= selectionEndLine) ? selectionStartCol : selectionEndCol;
        int endCol = (selectionStartLine <= selectionEndLine) ? selectionEndCol : selectionStartCol;

        // Swap if selecting backwards
        if (selectionStartLine > selectionEndLine ||
            (selectionStartLine == selectionEndLine && selectionStartCol > selectionEndCol)) {
            std::swap(startCol, endCol);
        }

        std::string result;
        for (int i = startLine; i <= endLine && i < static_cast<int>(wrappedLines.size()); ++i) {
            const auto& line = wrappedLines[i];

            int lineStart = (i == startLine) ? startCol : 0;
            int lineEnd = (i == endLine) ? endCol : static_cast<int>(line.length());

            lineStart = std::max(0, std::min(lineStart, static_cast<int>(line.length())));
            lineEnd = std::max(0, std::min(lineEnd, static_cast<int>(line.length())));

            if (lineStart < lineEnd) {
                result += line.substr(lineStart, lineEnd - lineStart);
            }

            if (i < endLine) {
                result += "\n";
            }
        }

        return result;
    }

    void selectAllText() {
        if (wrappedLines.empty()) return;

        hasSelection = true;
        selectionStartLine = 0;
        selectionStartCol = 0;
        selectionEndLine = static_cast<int>(wrappedLines.size()) - 1;
        selectionEndCol = static_cast<int>(wrappedLines.back().length());
    }

    void clearSelectionState() {
        hasSelection = false;
        selecting = false;
        selectionStartLine = -1;
        selectionStartCol = -1;
        selectionEndLine = -1;
        selectionEndCol = -1;
    }
};

ConsolePanel::ConsolePanel() {
    m_config.id = "console";
    m_config.title = "Console";
    m_config.bounds = {20, 400, 800, 300};
    m_config.dockSide = DockSide::None;
    m_config.visible = false;
    m_config.resizable = true;
    m_config.draggable = true;
    m_config.minWidth = 300.0f;
    m_config.minHeight = 150.0f;
}

ConsolePanel::~ConsolePanel() {
    shutdown();
}

bool ConsolePanel::init(Context& ctx, WGPUTextureFormat surfaceFormat) {
    m_impl = std::make_unique<Impl>();
    m_impl->style.scale = ctx.contentScale();
    return true;
}

void ConsolePanel::shutdown() {
    m_impl.reset();
}

void ConsolePanel::update() {
    // Nothing to update each frame
}

void ConsolePanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                           const FrameInput& input, const UIStyle& style) {
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
    float contentY = y + titleBarHeight;
    float contentH = h - titleBarHeight;

    // Render chrome (title bar controlled by m_display.showTitleBar)
    renderChrome(canvas, x, y, w, h, style, m_display.showTitleBar, &input);

    // Get font metrics (monospace font at index 2)
    int fontIndex = 2;
    float lineHeight = canvas.fontLineHeight(fontIndex);
    float charWidth = canvas.measureText("M", fontIndex);

    if (lineHeight <= 0 || charWidth <= 0) {
        lineHeight = 14.0f;
        charWidth = 8.0f;
    }

    m_impl->lineHeight = lineHeight;
    m_impl->charWidth = charWidth;

    // Content area
    float padding = 8;
    float contentX = x + padding;
    float contentW = w - padding * 2;
    contentY += padding;
    contentH -= padding * 2;

    // Calculate chars per line for word wrap
    m_impl->charsPerLine = static_cast<int>(contentW / charWidth);
    if (m_impl->charsPerLine < 40) m_impl->charsPerLine = 40;

    // Rebuild wrapped lines if needed
    m_impl->rebuildWrappedLines();

    // Background
    canvas.fillRect(contentX, contentY, contentW, contentH, m_impl->style.terminalBg);

    // Calculate total content height
    m_impl->contentHeight = static_cast<float>(m_impl->wrappedLines.size()) * lineHeight;
    m_impl->maxScroll = std::max(0.0f, m_impl->contentHeight - contentH);

    // Clamp scroll
    m_impl->scrollOffset = std::clamp(m_impl->scrollOffset, 0.0f, m_impl->maxScroll);

    // Clip to content area
    canvas.beginClipRect(contentX, contentY, contentW, contentH);

    // Calculate visible line range
    int firstVisibleLine = static_cast<int>(m_impl->scrollOffset / lineHeight);
    int visibleLineCount = static_cast<int>(contentH / lineHeight) + 2;

    // Mouse position for selection
    float mouseX = input.mousePos.x;
    float mouseY = input.mousePos.y;

    // Handle mouse selection
    bool mouseInContent = mouseX >= contentX && mouseX < contentX + contentW &&
                          mouseY >= contentY && mouseY < contentY + contentH;

    if (mouseInContent && input.mouseDown[0]) {
        // Calculate line and column from mouse position
        int clickedLine = firstVisibleLine + static_cast<int>((mouseY - contentY) / lineHeight);
        int clickedCol = static_cast<int>((mouseX - contentX) / charWidth);

        clickedLine = std::max(0, std::min(clickedLine, static_cast<int>(m_impl->wrappedLines.size()) - 1));
        if (clickedLine < static_cast<int>(m_impl->wrappedLines.size())) {
            clickedCol = std::max(0, std::min(clickedCol, static_cast<int>(m_impl->wrappedLines[clickedLine].length())));
        }

        if (!m_impl->selecting) {
            // Start selection
            m_impl->selecting = true;
            m_impl->selectionStartLine = clickedLine;
            m_impl->selectionStartCol = clickedCol;
            m_impl->selectionEndLine = clickedLine;
            m_impl->selectionEndCol = clickedCol;
            m_impl->hasSelection = false;
        } else {
            // Continue selection
            m_impl->selectionEndLine = clickedLine;
            m_impl->selectionEndCol = clickedCol;
            m_impl->hasSelection = (m_impl->selectionStartLine != m_impl->selectionEndLine ||
                                     m_impl->selectionStartCol != m_impl->selectionEndCol);
        }
        m_inputRouting.consumedInput = true;
    } else {
        m_impl->selecting = false;
    }

    // Render lines
    for (int i = firstVisibleLine; i < firstVisibleLine + visibleLineCount; ++i) {
        if (i < 0 || i >= static_cast<int>(m_impl->wrappedLines.size())) continue;

        const auto& line = m_impl->wrappedLines[i];
        size_t msgIndex = m_impl->lineToMsgIndex[i];
        const auto& msg = m_impl->messages[msgIndex];

        float lineY = contentY + (i - firstVisibleLine) * lineHeight - std::fmod(m_impl->scrollOffset, lineHeight);
        float textY = lineY + lineHeight;

        // Draw selection highlight
        if (m_impl->hasSelection) {
            int startLine = std::min(m_impl->selectionStartLine, m_impl->selectionEndLine);
            int endLine = std::max(m_impl->selectionStartLine, m_impl->selectionEndLine);

            if (i >= startLine && i <= endLine) {
                int startCol = 0;
                int endCol = static_cast<int>(line.length());

                if (i == startLine) {
                    startCol = (m_impl->selectionStartLine <= m_impl->selectionEndLine)
                        ? m_impl->selectionStartCol : m_impl->selectionEndCol;
                }
                if (i == endLine) {
                    endCol = (m_impl->selectionStartLine <= m_impl->selectionEndLine)
                        ? m_impl->selectionEndCol : m_impl->selectionStartCol;
                }
                if (startLine == endLine && m_impl->selectionStartCol > m_impl->selectionEndCol) {
                    std::swap(startCol, endCol);
                }

                startCol = std::max(0, std::min(startCol, static_cast<int>(line.length())));
                endCol = std::max(0, std::min(endCol, static_cast<int>(line.length())));

                if (startCol < endCol) {
                    float selX = contentX + startCol * charWidth;
                    float selW = (endCol - startCol) * charWidth;
                    canvas.fillRect(selX, lineY, selW, lineHeight, m_impl->style.terminalSelection);
                }
            }
        }

        // Get color for message type
        glm::vec4 color = m_impl->getColorForType(msg.type);

        // Render line with color coding
        // Timestamp is always dim
        size_t timestampEnd = line.find("] ");
        if (timestampEnd != std::string::npos) {
            timestampEnd += 2;
            std::string timestampPart = line.substr(0, timestampEnd);
            canvas.text(timestampPart.c_str(), contentX, textY, m_impl->style.textDim, fontIndex);

            // Render rest of line in message color
            if (timestampEnd < line.length()) {
                std::string restPart = line.substr(timestampEnd);
                float restX = contentX + timestampEnd * charWidth;
                canvas.text(restPart.c_str(), restX, textY, color, fontIndex);
            }
        } else {
            canvas.text(line.c_str(), contentX, textY, color, fontIndex);
        }
    }

    canvas.endClipRect();

    // Draw scrollbar if content exceeds view
    if (m_impl->maxScroll > 0) {
        float scrollbarWidth = 8;
        float scrollbarX = x + w - scrollbarWidth - 2;
        float scrollbarTrackY = contentY;
        float scrollbarTrackH = contentH;

        // Track background
        canvas.fillRect(scrollbarX, scrollbarTrackY, scrollbarWidth, scrollbarTrackH,
                       glm::vec4(0.2f, 0.2f, 0.2f, 0.5f));

        // Thumb
        float thumbRatio = contentH / m_impl->contentHeight;
        float thumbHeight = std::max(20.0f, scrollbarTrackH * thumbRatio);
        float thumbY = scrollbarTrackY + (m_impl->scrollOffset / m_impl->maxScroll) * (scrollbarTrackH - thumbHeight);

        canvas.fillRect(scrollbarX, thumbY, scrollbarWidth, thumbHeight,
                       glm::vec4(0.5f, 0.5f, 0.5f, 0.7f));
    }

    // Check hover state
    m_focus.hovered = mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + h;
}

bool ConsolePanel::handleInput(const FrameInput& input) {
    if (!m_focus.focused || !m_impl) return false;

    // Handle scroll
    if (input.scroll.y != 0) {
        m_impl->scrollOffset -= input.scroll.y * m_impl->style.scrollSpeed();
        m_impl->scrollOffset = std::clamp(m_impl->scrollOffset, 0.0f, m_impl->maxScroll);
        return true;
    }

    return false;
}

void ConsolePanel::onChar(uint32_t codepoint) {
    // Console doesn't accept text input
}

void ConsolePanel::onKeyDown(int key, int mods) {
    if (!m_impl) return;

    bool ctrl = (mods & 0x2) != 0;
    bool cmd = (mods & 0x8) != 0;
    bool cmdOrCtrl = ctrl || cmd;

    // Cmd+C / Ctrl+C: Copy
    if (cmdOrCtrl && key == 67) {  // 'C'
        copySelection();
        return;
    }

    // Cmd+A / Ctrl+A: Select all
    if (cmdOrCtrl && key == 65) {  // 'A'
        selectAll();
        return;
    }

    // Escape: Clear selection
    if (key == 256) {  // GLFW_KEY_ESCAPE
        clearSelection();
        return;
    }

    // Page Up
    if (key == 266) {
        float pageSize = (m_config.bounds.w - m_impl->style.titleBarHeight()) * 0.9f;
        m_impl->scrollOffset = std::max(0.0f, m_impl->scrollOffset - pageSize);
        return;
    }

    // Page Down
    if (key == 267) {
        float pageSize = (m_config.bounds.w - m_impl->style.titleBarHeight()) * 0.9f;
        m_impl->scrollOffset = std::min(m_impl->maxScroll, m_impl->scrollOffset + pageSize);
        return;
    }

    // Home
    if (key == 268) {
        m_impl->scrollOffset = 0;
        return;
    }

    // End
    if (key == 269) {
        m_impl->scrollOffset = m_impl->maxScroll;
        return;
    }
}

void ConsolePanel::setCompileErrors(const std::vector<CompileError>& errors) {
    if (!m_impl) return;

    // Clear existing compile errors but keep other messages
    auto it = std::remove_if(m_impl->messages.begin(), m_impl->messages.end(),
        [](const ConsoleMessage& msg) {
            return msg.type == ConsoleMessageType::CompileError;
        });
    m_impl->messages.erase(it, m_impl->messages.end());

    // Add new compile errors
    for (const auto& error : errors) {
        ConsoleMessage msg;
        msg.type = ConsoleMessageType::CompileError;
        msg.text = error.message;
        msg.file = error.file;
        msg.line = error.line;
        msg.timestamp = Impl::getCurrentTimestamp();
        m_impl->messages.push_back(std::move(msg));
    }

    m_hasErrors = !errors.empty();

    // Trim old messages if needed
    while (m_impl->messages.size() > MAX_MESSAGES) {
        m_impl->messages.erase(m_impl->messages.begin());
    }

    // Auto-scroll to bottom
    if (m_autoScroll && !errors.empty()) {
        m_impl->scrollOffset = m_impl->maxScroll;
    }

    // Auto-show panel when errors occur
    if (!errors.empty()) {
        m_config.visible = true;
    }
}

void ConsolePanel::clearCompileErrors() {
    if (!m_impl) return;

    auto it = std::remove_if(m_impl->messages.begin(), m_impl->messages.end(),
        [](const ConsoleMessage& msg) {
            return msg.type == ConsoleMessageType::CompileError;
        });
    m_impl->messages.erase(it, m_impl->messages.end());

    m_hasErrors = false;
}

void ConsolePanel::addMessage(ConsoleMessageType type, const std::string& message) {
    addMessage(type, message, "", 0);
}

void ConsolePanel::addMessage(ConsoleMessageType type, const std::string& message,
                               const std::string& file, int line) {
    if (!m_impl) return;

    ConsoleMessage msg;
    msg.type = type;
    msg.text = message;
    msg.file = file;
    msg.line = line;
    msg.timestamp = Impl::getCurrentTimestamp();

    m_impl->messages.push_back(std::move(msg));

    // Trim old messages if needed
    while (m_impl->messages.size() > MAX_MESSAGES) {
        m_impl->messages.erase(m_impl->messages.begin());
    }

    // Track error state
    if (type == ConsoleMessageType::Error || type == ConsoleMessageType::CompileError) {
        m_hasErrors = true;
    }

    // Auto-scroll to bottom
    if (m_autoScroll) {
        m_impl->scrollOffset = m_impl->maxScroll;
    }
}

void ConsolePanel::clear() {
    if (!m_impl) return;

    m_impl->messages.clear();
    m_impl->wrappedLines.clear();
    m_impl->lineToMsgIndex.clear();
    m_impl->scrollOffset = 0;
    m_impl->clearSelectionState();
    m_hasErrors = false;
}

size_t ConsolePanel::messageCount() const {
    return m_impl ? m_impl->messages.size() : 0;
}

void ConsolePanel::setClipboardCallbacks(
    std::function<std::string()> getClipboard,
    std::function<void(const std::string&)> setClipboard) {
    if (!m_impl) return;
    m_impl->getClipboard = std::move(getClipboard);
    m_impl->setClipboard = std::move(setClipboard);
}

void ConsolePanel::copySelection() {
    if (!m_impl || !m_impl->setClipboard) return;

    std::string selected = m_impl->getSelectedText();
    if (!selected.empty()) {
        m_impl->setClipboard(selected);
    }
}

void ConsolePanel::selectAll() {
    if (!m_impl) return;
    m_impl->selectAllText();
}

void ConsolePanel::clearSelection() {
    if (!m_impl) return;
    m_impl->clearSelectionState();
}

} // namespace vivid
