// Terminal Panel Implementation
// Uses libvterm for VT220/xterm emulation and PTY for shell integration
// Migrated from vivid-ide to vivid-devtools

#include <vivid/devtools/panels/terminal_panel.h>
#include <vivid/context.h>
#include <vivid/gui/ui_style.h>
#include <vivid/pty.h>

#include <vterm.h>

#include <vector>
#include <algorithm>
#include <iostream>
#include <cstring>

namespace vivid {

// Convert libvterm color to glm::vec4
static glm::vec4 vtermColorToVec4(const VTermColor& color, const VTermScreen* screen, bool isForeground, const UIStyle& style) {
    VTermColor col = color;

    // Check for default colors - use style colors
    if (VTERM_COLOR_IS_DEFAULT_FG(&col)) {
        return style.terminalFg;
    }
    if (VTERM_COLOR_IS_DEFAULT_BG(&col)) {
        return style.terminalBg;
    }

    // Convert indexed colors to RGB
    if (VTERM_COLOR_IS_INDEXED(&col)) {
        vterm_screen_convert_color_to_rgb(screen, &col);
    }

    // Now it should be RGB
    if (VTERM_COLOR_IS_RGB(&col)) {
        return glm::vec4(
            col.rgb.red / 255.0f,
            col.rgb.green / 255.0f,
            col.rgb.blue / 255.0f,
            1.0f
        );
    }

    // Fallback
    return isForeground ? style.terminalFg : style.terminalBg;
}

struct TerminalPanel::Impl {
    VTerm* vt = nullptr;
    VTermScreen* screen = nullptr;
    std::unique_ptr<PTY> pty;
    int cols = 80;
    int rows = 24;
    bool cursorVisible = true;
    bool cursorBlink = true;
    int cursorBlinkFrame = 0;
    UIStyle style;  // Copy of style for rendering

    // Scroll state
    int scrollOffset = 0;

    // Scrollback buffer
    struct ScrollLine {
        std::vector<VTermScreenCell> cells;
    };
    std::vector<ScrollLine> scrollback;
    static constexpr int MAX_SCROLLBACK = 1000;

    // Exit callback
    std::function<void(int)> onExit;

    // Output callback - sends data from terminal back to PTY
    static void outputCallback(const char* s, size_t len, void* user) {
        auto* impl = static_cast<Impl*>(user);
        if (impl->pty && len > 0) {
            impl->pty->write(std::string(s, len));
        }
    }

    // Screen callback for cursor movement - tracks visibility
    static int moveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user) {
        auto* impl = static_cast<Impl*>(user);
        impl->cursorVisible = (visible != 0);
        return 1;
    }

    // Screen callbacks for scrollback
    static int sbPushLine(int cols, const VTermScreenCell* cells, void* user) {
        auto* impl = static_cast<Impl*>(user);
        if (impl->scrollback.size() >= MAX_SCROLLBACK) {
            impl->scrollback.erase(impl->scrollback.begin());
        }
        ScrollLine line;
        line.cells.assign(cells, cells + cols);
        impl->scrollback.push_back(std::move(line));
        return 1;
    }

    static int sbPopLine(int cols, VTermScreenCell* cells, void* user) {
        auto* impl = static_cast<Impl*>(user);
        if (impl->scrollback.empty()) {
            return 0;
        }
        auto& line = impl->scrollback.back();
        size_t copyLen = std::min(static_cast<size_t>(cols), line.cells.size());
        std::memcpy(cells, line.cells.data(), copyLen * sizeof(VTermScreenCell));
        impl->scrollback.pop_back();
        return 1;
    }

    void scroll(int delta) {
        scrollOffset = std::max(0, scrollOffset + delta);
        scrollOffset = std::min(scrollOffset, static_cast<int>(scrollback.size()));
    }

    void scrollToBottom() {
        scrollOffset = 0;
    }
};

TerminalPanel::TerminalPanel() {
    m_config.id = "terminal";
    m_config.title = "Terminal";
    m_config.bounds = {20, 60, 900, 600};
    m_config.dockSide = DockSide::None;
    m_config.visible = false;
    m_config.resizable = true;
    m_config.draggable = true;
    m_config.minWidth = 400.0f;
    m_config.minHeight = 200.0f;
}

TerminalPanel::~TerminalPanel() {
    shutdown();
}

bool TerminalPanel::init(Context& ctx, WGPUTextureFormat surfaceFormat) {
    m_impl = std::make_unique<Impl>();
    m_impl->style.scale = ctx.contentScale();

    // Create terminal emulator
    m_impl->vt = vterm_new(m_rows, m_cols);
    if (!m_impl->vt) {
        return false;
    }

    // Enable UTF-8
    vterm_set_utf8(m_impl->vt, 1);

    // Set output callback for data going back to PTY
    vterm_output_set_callback(m_impl->vt, Impl::outputCallback, m_impl.get());

    // Get screen layer
    m_impl->screen = vterm_obtain_screen(m_impl->vt);

    // Enable alternate screen buffer (for apps like vim, htop, etc.)
    vterm_screen_enable_altscreen(m_impl->screen, 1);

    // Set up screen callbacks for scrollback and cursor tracking
    static VTermScreenCallbacks screenCallbacks = {};
    screenCallbacks.movecursor = Impl::moveCursor;
    screenCallbacks.sb_pushline = Impl::sbPushLine;
    screenCallbacks.sb_popline = Impl::sbPopLine;
    vterm_screen_set_callbacks(m_impl->screen, &screenCallbacks, m_impl.get());

    // Reset screen
    vterm_screen_reset(m_impl->screen, 1);

    // Create PTY
    m_impl->pty = std::make_unique<PTY>();
    return true;
}

void TerminalPanel::shutdown() {
    stop();
    if (m_impl && m_impl->vt) {
        vterm_free(m_impl->vt);
        m_impl->vt = nullptr;
    }
    m_impl.reset();
}

void TerminalPanel::update() {
    if (!m_impl || !m_impl->pty || !m_impl->vt) return;

    // Read from PTY and feed to terminal emulator
    std::string output = m_impl->pty->read();
    if (!output.empty()) {
        vterm_input_write(m_impl->vt, output.c_str(), output.size());
    }

    // Cursor blink animation
    m_impl->cursorBlinkFrame++;
    if (m_impl->cursorBlinkFrame >= 30) {  // ~0.5s at 60fps
        m_impl->cursorBlink = !m_impl->cursorBlink;
        m_impl->cursorBlinkFrame = 0;
    }

    // Check if process exited
    if (!m_impl->pty->isRunning() && m_impl->onExit) {
        m_impl->onExit(0);
    }
}

void TerminalPanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                            const gui::InputState& input, const UIStyle& style) {
    if (!m_config.visible || !m_impl || !m_impl->vt || !m_impl->screen) {
        m_inputRouting.consumedInput = false;
        m_focus.hovered = false;
        return;
    }

    // Store style for use in color conversion
    m_impl->style = style;

    glm::vec4 renderBounds = beginRender(input, bounds);
    float x = renderBounds.x;
    float y = renderBounds.y;
    float w = renderBounds.z;
    float h = renderBounds.w;

    // Debug: log render bounds once
    static bool loggedTerminalBounds = false;
    if (!loggedTerminalBounds && !m_display.showTitleBar) {
        std::cerr << "[TerminalPanel] showTitleBar=" << m_display.showTitleBar
                  << " passed bounds: " << bounds.x << "," << bounds.y << " " << bounds.z << "x" << bounds.w
                  << " renderBounds: " << x << "," << y << " " << w << "x" << h << "\n";
        loggedTerminalBounds = true;
    }

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
        lineHeight = 16.0f;
        charWidth = 8.0f;
    }

    // Content area
    float padding = 4.0f;
    float contentX = x + padding;
    float contentW = w - padding * 2;
    contentY += padding;
    contentH -= padding;

    // Background
    canvas.fillRect(contentX, contentY, contentW, contentH, m_impl->style.terminalBg);

    // Clip to content area
    canvas.beginClipRect(contentX, contentY, contentW, contentH);

    // Calculate terminal dimensions based on content area
    int newCols = static_cast<int>(contentW / charWidth);
    int newRows = static_cast<int>(contentH / lineHeight);
    newCols = std::max(10, newCols);  // Minimum 10 columns
    newRows = std::max(3, newRows);   // Minimum 3 rows

    // Resize terminal if dimensions changed
    if (newCols != m_cols || newRows != m_rows) {
        resize(newCols, newRows);
    }

    // Get terminal dimensions
    int termRows, termCols;
    vterm_get_size(m_impl->vt, &termRows, &termCols);

    // Calculate visible rows
    int visibleRows = static_cast<int>(contentH / lineHeight);

    // Get cursor position
    VTermPos cursorPos;
    VTermState* state = vterm_obtain_state(m_impl->vt);
    vterm_state_get_cursorpos(state, &cursorPos);

    // Render each line
    for (int row = 0; row < termRows && row < visibleRows; row++) {
        float lineY = contentY + (row + 1) * lineHeight;

        for (int col = 0; col < termCols; col++) {
            VTermPos pos = {row, col};
            VTermScreenCell cell;
            vterm_screen_get_cell(m_impl->screen, pos, &cell);

            // Skip empty cells
            if (cell.chars[0] == 0 || cell.chars[0] == ' ') {
                // Still draw background if not default
                if (!VTERM_COLOR_IS_DEFAULT_BG(&cell.bg)) {
                    glm::vec4 bg = vtermColorToVec4(cell.bg, m_impl->screen, false, m_impl->style);
                    float charX = contentX + col * charWidth;
                    canvas.fillRect(charX, lineY - lineHeight + 2, charWidth * cell.width, lineHeight, bg);
                }
                continue;
            }

            // Get colors
            glm::vec4 fg = vtermColorToVec4(cell.fg, m_impl->screen, true, m_impl->style);
            glm::vec4 bg = vtermColorToVec4(cell.bg, m_impl->screen, false, m_impl->style);

            // Handle attributes
            if (cell.attrs.reverse) {
                std::swap(fg, bg);
            }

            if (cell.attrs.bold) {
                fg = glm::vec4(std::min(fg.r * 1.3f, 1.0f),
                               std::min(fg.g * 1.3f, 1.0f),
                               std::min(fg.b * 1.3f, 1.0f), 1.0f);
            }

            if (cell.attrs.conceal) {
                fg.a = 0.0f;
            }

            float charX = contentX + col * charWidth;

            // Draw background if not default
            if (!VTERM_COLOR_IS_DEFAULT_BG(&cell.bg) || cell.attrs.reverse) {
                canvas.fillRect(charX, lineY - lineHeight + 2, charWidth * cell.width, lineHeight, bg);
            }

            // Skip hidden text
            if (cell.attrs.conceal) {
                if (cell.width > 1) col += cell.width - 1;
                continue;
            }

            // Convert UTF-32 to UTF-8
            char utf8[32] = {0};
            int utf8Len = 0;
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; i++) {
                uint32_t c = cell.chars[i];
                if (c < 0x80) {
                    utf8[utf8Len++] = static_cast<char>(c);
                } else if (c < 0x800) {
                    utf8[utf8Len++] = static_cast<char>(0xC0 | (c >> 6));
                    utf8[utf8Len++] = static_cast<char>(0x80 | (c & 0x3F));
                } else if (c < 0x10000) {
                    utf8[utf8Len++] = static_cast<char>(0xE0 | (c >> 12));
                    utf8[utf8Len++] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                    utf8[utf8Len++] = static_cast<char>(0x80 | (c & 0x3F));
                } else {
                    utf8[utf8Len++] = static_cast<char>(0xF0 | (c >> 18));
                    utf8[utf8Len++] = static_cast<char>(0x80 | ((c >> 12) & 0x3F));
                    utf8[utf8Len++] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                    utf8[utf8Len++] = static_cast<char>(0x80 | (c & 0x3F));
                }
                if (utf8Len >= 28) break;
            }
            utf8[utf8Len] = 0;

            canvas.text(utf8, charX, lineY, fg, fontIndex);

            // Underline
            if (cell.attrs.underline) {
                canvas.line(charX, lineY + 1, charX + charWidth * cell.width, lineY + 1, 1.0f, fg);
            }

            // Strikethrough
            if (cell.attrs.strike) {
                float strikeY = lineY - lineHeight / 3;
                canvas.line(charX, strikeY, charX + charWidth * cell.width, strikeY, 1.0f, fg);
            }

            // Skip wide character's second cell
            if (cell.width > 1) {
                col += cell.width - 1;
            }
        }
    }

    // Draw cursor
    if (m_focus.focused && m_impl->cursorVisible && m_impl->cursorBlink) {
        float cursorX = contentX + cursorPos.col * charWidth;
        float cursorY = contentY + cursorPos.row * lineHeight;
        canvas.fillRect(cursorX, cursorY, charWidth, lineHeight, m_impl->style.terminalCursor);
    }

    canvas.endClipRect();
}

bool TerminalPanel::handleInput(const gui::InputState& input) {
    if (!m_focus.focused) return false;

    // Handle scroll
    if (input.scroll.y != 0) {
        m_impl->scroll(static_cast<int>(-input.scroll.y * 3));
        return true;
    }

    return false;
}

void TerminalPanel::onChar(uint32_t codepoint) {
    if (!m_impl || !m_impl->vt) return;
    vterm_keyboard_unichar(m_impl->vt, codepoint, VTERM_MOD_NONE);
    m_impl->scrollToBottom();
}

void TerminalPanel::onKeyDown(int key, int mods) {
    if (!m_impl || !m_impl->vt) return;

    // Convert GLFW modifiers to VTerm modifiers
    VTermModifier vtMod = VTERM_MOD_NONE;
    if (mods & 0x1) vtMod = static_cast<VTermModifier>(vtMod | VTERM_MOD_SHIFT);
    if (mods & 0x2) vtMod = static_cast<VTermModifier>(vtMod | VTERM_MOD_CTRL);
    if (mods & 0x4) vtMod = static_cast<VTermModifier>(vtMod | VTERM_MOD_ALT);

    VTermKey vtKey = VTERM_KEY_NONE;

    switch (key) {
        case 257: vtKey = VTERM_KEY_ENTER; break;
        case 259: vtKey = VTERM_KEY_BACKSPACE; break;
        case 258: vtKey = VTERM_KEY_TAB; break;
        case 256: vtKey = VTERM_KEY_ESCAPE; break;
        case 265: vtKey = VTERM_KEY_UP; break;
        case 264: vtKey = VTERM_KEY_DOWN; break;
        case 263: vtKey = VTERM_KEY_LEFT; break;
        case 262: vtKey = VTERM_KEY_RIGHT; break;
        case 268: vtKey = VTERM_KEY_HOME; break;
        case 269: vtKey = VTERM_KEY_END; break;
        case 266: vtKey = VTERM_KEY_PAGEUP; break;
        case 267: vtKey = VTERM_KEY_PAGEDOWN; break;
        case 261: vtKey = VTERM_KEY_DEL; break;
        case 260: vtKey = VTERM_KEY_INS; break;
        case 290: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(1)); break;
        case 291: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(2)); break;
        case 292: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(3)); break;
        case 293: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(4)); break;
        case 294: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(5)); break;
        case 295: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(6)); break;
        case 296: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(7)); break;
        case 297: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(8)); break;
        case 298: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(9)); break;
        case 299: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(10)); break;
        case 300: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(11)); break;
        case 301: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(12)); break;
        default:
            // For Ctrl+letter combinations
            if ((mods & 0x2) && key >= 65 && key <= 90) {
                vterm_keyboard_unichar(m_impl->vt, key, vtMod);
                m_impl->scrollToBottom();
                return;
            }
            break;
    }

    if (vtKey != VTERM_KEY_NONE) {
        vterm_keyboard_key(m_impl->vt, vtKey, vtMod);
        m_impl->scrollToBottom();
    }
}

void TerminalPanel::spawn(const std::string& command, const std::string& workingDir) {
    if (!m_impl || !m_impl->pty) {
        return;
    }

    bool ok = m_impl->pty->start(command, workingDir);
    if (ok) {
        m_impl->pty->setSize(m_cols, m_rows);
        m_running = true;
    }
}

void TerminalPanel::stop() {
    if (m_impl && m_impl->pty) {
        m_impl->pty->stop();
    }
    m_running = false;
}

void TerminalPanel::resize(int cols, int rows) {
    m_cols = cols;
    m_rows = rows;
    if (m_impl) {
        m_impl->cols = cols;
        m_impl->rows = rows;
        if (m_impl->vt) {
            vterm_set_size(m_impl->vt, rows, cols);
        }
        if (m_impl->pty) {
            m_impl->pty->setSize(cols, rows);
        }
    }
}

} // namespace vivid
