#include <vivid/effects/window_events.h>

namespace vivid {

void WindowEvents::init(Context& ctx) {
    if (!beginInit()) return;

    // Initialize with current window size
    m_width = ctx.width();
    m_height = ctx.height();
    m_lastWidth = m_width;
    m_lastHeight = m_height;
}

void WindowEvents::clearFrameState() {
    m_frameEvents.clear();
    m_resized = false;
    m_focused = false;
    m_unfocused = false;
}

void WindowEvents::process(Context& ctx) {
    clearFrameState();

    // Get current window size
    m_width = ctx.width();
    m_height = ctx.height();

    // Check for resize using Context's wasResized() method
    if (ctx.wasResized()) {
        m_resized = true;
        m_frameEvents.push_back(Event::windowResize(m_width, m_height));
    }

    m_lastWidth = m_width;
    m_lastHeight = m_height;

    // Note: Focus tracking not currently available in Context
    // Focus events will be added when Context supports focus state
}

} // namespace vivid
