#include <vivid/effects/mouse_in.h>

namespace vivid {

MouseIn::MouseIn() {
    m_buttonHeld.fill(false);
    m_buttonPressedThisFrame.fill(false);
    m_buttonReleasedThisFrame.fill(false);
}

void MouseIn::init(Context& ctx) {
    if (!beginInit()) return;
}

void MouseIn::clearFrameState() {
    m_frameEvents.clear();
    m_buttonPressedThisFrame.fill(false);
    m_buttonReleasedThisFrame.fill(false);
    m_anyButtonPressed = false;
    m_scroll = {0, 0};
}

void MouseIn::process(Context& ctx) {
    clearFrameState();

    // Get current position
    m_position = ctx.mouse();
    m_positionNorm = ctx.mouseNorm();

    // Calculate delta (skip on first frame to avoid huge jump)
    if (m_firstFrame) {
        m_lastPosition = m_position;
        m_firstFrame = false;
    }
    m_delta = ctx.mouseDelta();
    m_deltaNorm = ctx.mouseDeltaNorm();

    // Generate move event if position changed
    if (m_delta.x != 0.0f || m_delta.y != 0.0f) {
        m_frameEvents.push_back(Event::mouseMove(m_position.x, m_position.y));
    }

    // Check buttons
    for (int btn = 0; btn < MAX_BUTTONS; ++btn) {
        const auto& state = ctx.mouseButton(btn);

        m_buttonHeld[btn] = state.held;

        if (state.pressed) {
            m_buttonPressedThisFrame[btn] = true;
            m_anyButtonPressed = true;
            m_frameEvents.push_back(Event::mousePress(btn, m_position.x, m_position.y));
        }

        if (state.released) {
            m_buttonReleasedThisFrame[btn] = true;
            m_frameEvents.push_back(Event::mouseRelease(btn, m_position.x, m_position.y));
        }
    }

    // Check scroll
    m_scroll = ctx.scroll();
    if (m_scroll.x != 0.0f || m_scroll.y != 0.0f) {
        m_frameEvents.push_back(Event::mouseScroll(m_scroll.x, m_scroll.y));
    }

    m_lastPosition = m_position;
}

bool MouseIn::buttonPressed(int button) const {
    if (button < 0 || button >= MAX_BUTTONS) return false;
    return m_buttonPressedThisFrame[button];
}

bool MouseIn::buttonHeld(int button) const {
    if (button < 0 || button >= MAX_BUTTONS) return false;
    return m_buttonHeld[button];
}

bool MouseIn::buttonReleased(int button) const {
    if (button < 0 || button >= MAX_BUTTONS) return false;
    return m_buttonReleasedThisFrame[button];
}

} // namespace vivid
