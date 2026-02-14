// Console Panel Implementation
// Read-only scrolling log overlay for runtime messages

#include <vivid/devtools/panels/console_panel.h>
#include <vivid/context.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/ui_style.h>
#include <array>
#include <mutex>
#include <vector>
#include <chrono>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace vivid {

static constexpr size_t BUFFER_SIZE = 256;

struct LogMessage {
    LogLevel level = LogLevel::Info;
    std::string message;
    std::string timestamp;  // "HH:MM:SS"
};

struct ConsolePanel::Impl {
    // Thread-safe ring buffer (written by Log callback from any thread)
    std::mutex mutex;
    std::array<LogMessage, BUFFER_SIZE> messages;
    size_t writeIndex = 0;
    size_t count = 0;
    bool dirty = true;  // Start dirty so initial render builds snapshot

    // Render-side snapshot (main thread only, rebuilt when dirty)
    std::vector<LogMessage> snapshot;

    // Scroll state
    float scrollOffset = 0.0f;  // 0 = at bottom (newest), positive = scrolled up
    bool autoScroll = true;
};

ConsolePanel::ConsolePanel()
    : m_impl(std::make_unique<Impl>())
{
    m_config.id = "console";
    m_config.title = "Console";
    m_config.bounds = {10, 500, 600, 180};
    m_config.role = PanelRole::Floating;
    m_config.visible = false;
    m_config.resizable = true;
    m_config.draggable = true;
    m_config.minWidth = 300.0f;
    m_config.minHeight = 100.0f;
}

ConsolePanel::~ConsolePanel() = default;

bool ConsolePanel::init(Context& /*ctx*/, WGPUTextureFormat /*surfaceFormat*/) {
    return true;
}

void ConsolePanel::shutdown() {
    m_impl.reset();
}

void ConsolePanel::pushMessage(LogLevel level, const char* file, int line, const std::string& message) {
    // Format timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    auto& msg = m_impl->messages[m_impl->writeIndex];
    msg.level = level;
    msg.message = message;
    msg.timestamp = timeBuf;

    m_impl->writeIndex = (m_impl->writeIndex + 1) % BUFFER_SIZE;
    if (m_impl->count < BUFFER_SIZE) m_impl->count++;
    m_impl->dirty = true;
}

void ConsolePanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                           const gui::InputState& input, const UIStyle& style) {
    if (!m_config.visible || !m_impl) return;

    glm::vec4 renderBounds = beginRender(input, bounds);
    float x = renderBounds.x;
    float y = renderBounds.y;
    float w = renderBounds.z;
    float h = renderBounds.w;

    canvas.setLayer(UILayer::FloatingPanels);

    // Semi-transparent background (doesn't fully obscure output)
    float cornerRadius = style.panelCornerRadius();
    glm::vec4 bgColor = style.panelBg;
    bgColor.a = 0.85f;

    if (cornerRadius > 0.0f) {
        canvas.fillRoundedRect(x, y, w, h, cornerRadius, bgColor);
        canvas.strokeRoundedRect(x, y, w, h, cornerRadius, 1.0f, style.panelBorder);
    } else {
        canvas.fillRect(x, y, w, h, bgColor);
        canvas.strokeRect(x, y, w, h, 1.0f, style.panelBorder);
    }

    // Title bar
    float titleH = style.titleBarHeight();
    glm::vec4 headerColor = style.headerBg;
    headerColor.a = 0.9f;
    canvas.fillRoundedRectTop(x, y, w, titleH, cornerRadius, headerColor);
    canvas.text(m_config.title, x + 10, y + 18, style.textPrimary, 0);

    // Close button (X) on title bar
    {
        float closeSize = 8.0f;
        float closePadding = 12.0f;
        float closeX = x + w - closePadding - closeSize;
        float closeY = y + titleH / 2;

        float hitPadding = 4.0f;
        bool overClose = input.mousePos.x >= closeX - closeSize - hitPadding &&
                         input.mousePos.x <= closeX + closeSize + hitPadding &&
                         input.mousePos.y >= closeY - closeSize - hitPadding &&
                         input.mousePos.y <= closeY + closeSize + hitPadding;

        if (overClose && input.mouseClicked[0]) {
            m_config.visible = false;
            return;
        }

        glm::vec4 closeColor = overClose
            ? glm::vec4(1.0f, 0.4f, 0.4f, 1.0f)
            : style.textDim;
        float lineWidth = overClose ? 2.0f : 1.5f;

        canvas.line(closeX - closeSize, closeY - closeSize,
                    closeX + closeSize, closeY + closeSize, lineWidth, closeColor);
        canvas.line(closeX + closeSize, closeY - closeSize,
                    closeX - closeSize, closeY + closeSize, lineWidth, closeColor);
    }

    // Rebuild snapshot from ring buffer if dirty
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (m_impl->dirty) {
            m_impl->snapshot.clear();
            m_impl->snapshot.reserve(m_impl->count);
            for (size_t i = 0; i < m_impl->count; i++) {
                size_t idx = (m_impl->count < BUFFER_SIZE)
                    ? i
                    : (m_impl->writeIndex + i) % BUFFER_SIZE;
                m_impl->snapshot.push_back(m_impl->messages[idx]);
            }
            m_impl->dirty = false;
        }
    }

    // Content area (below title bar)
    float contentX = x + style.padding();
    float contentY = y + titleH + style.smallPadding();
    float contentW = w - style.padding() * 2;
    float contentH = h - titleH - style.smallPadding() * 2;

    canvas.beginClipRect(x, y + titleH, w, h - titleH);

    const int fontIdx = 0;
    float lineH = canvas.fontLineHeight(fontIdx);
    if (lineH <= 0) lineH = 14.0f;
    lineH += 2.0f;  // Small padding between lines
    float ascent = canvas.fontAscent(fontIdx);
    if (ascent <= 0) ascent = 10.0f;

    size_t msgCount = m_impl->snapshot.size();
    float totalContentH = static_cast<float>(msgCount) * lineH;
    float maxScroll = std::max(0.0f, totalContentH - contentH);

    // Auto-scroll: snap to bottom when new messages arrive
    if (m_impl->autoScroll) {
        m_impl->scrollOffset = maxScroll;
    }
    m_impl->scrollOffset = std::clamp(m_impl->scrollOffset, 0.0f, maxScroll);

    // Handle scroll input
    if (m_focus.hovered && input.scroll.y != 0) {
        m_impl->scrollOffset -= input.scroll.y * style.scrollSpeed();
        m_impl->scrollOffset = std::clamp(m_impl->scrollOffset, 0.0f, maxScroll);
        // Re-enable auto-scroll when scrolled to bottom
        m_impl->autoScroll = (m_impl->scrollOffset >= maxScroll - 1.0f);
    }

    // Determine visible message range
    float cameraY = m_impl->scrollOffset;
    int firstVisible = std::max(0, static_cast<int>(cameraY / lineH));
    int lastVisible = std::min(static_cast<int>(msgCount) - 1,
                               static_cast<int>((cameraY + contentH) / lineH));

    // Level tag strings and colors
    auto levelTag = [](LogLevel level) -> const char* {
        switch (level) {
            case LogLevel::Debug: return "[DEBUG]";
            case LogLevel::Info:  return "[INFO]";
            case LogLevel::Warn:  return "[WARN]";
            case LogLevel::Error: return "[ERROR]";
            default: return "";
        }
    };
    auto levelColor = [&style](LogLevel level) -> glm::vec4 {
        switch (level) {
            case LogLevel::Debug: return style.textDim;
            case LogLevel::Info:  return style.textPrimary;
            case LogLevel::Warn:  return style.warning;
            case LogLevel::Error: return style.error;
            default: return style.textPrimary;
        }
    };

    for (int i = firstVisible; i <= lastVisible; i++) {
        const auto& msg = m_impl->snapshot[static_cast<size_t>(i)];
        float msgY = contentY + (static_cast<float>(i) * lineH - cameraY) + ascent;
        float curX = contentX;

        // Timestamp (dim)
        canvas.text(msg.timestamp, curX, msgY, style.textDim, fontIdx);
        curX += canvas.measureText(msg.timestamp, fontIdx) + 6.0f;

        // Level tag (colored)
        const char* tag = levelTag(msg.level);
        glm::vec4 tagColor = levelColor(msg.level);
        canvas.text(tag, curX, msgY, tagColor, fontIdx);
        curX += canvas.measureText(tag, fontIdx) + 6.0f;

        // Message text (primary for info/debug, severity color for warn/error)
        glm::vec4 msgColor = (msg.level >= LogLevel::Warn)
            ? tagColor : style.textPrimary;
        canvas.text(msg.message, curX, msgY, msgColor, fontIdx);
    }

    canvas.endClipRect();
}

bool ConsolePanel::handleInput(const gui::InputState& input) {
    if (!m_config.visible || !m_impl) return false;

    float x = m_config.bounds.x;
    float y = m_config.bounds.y;
    float w = m_config.bounds.z;
    float h = m_config.bounds.w;

    bool hovered = input.mousePos.x >= x && input.mousePos.x < x + w &&
                   input.mousePos.y >= y && input.mousePos.y < y + h;

    return hovered;
}

} // namespace vivid
