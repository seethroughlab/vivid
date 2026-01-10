#include <vivid/effects/keyboard_in.h>
#include <GLFW/glfw3.h>

namespace vivid {

KeyboardIn::KeyboardIn() {
    // Initialize arrays
    m_keyHeld.fill(false);
    m_keyPressedThisFrame.fill(false);
    m_keyReleasedThisFrame.fill(false);
}

void KeyboardIn::init(Context& ctx) {
    if (!beginInit()) return;
}

void KeyboardIn::clearFrameState() {
    m_frameEvents.clear();
    m_keyPressedThisFrame.fill(false);
    m_keyReleasedThisFrame.fill(false);
    m_anyKeyPressed = false;
}

void KeyboardIn::process(Context& ctx) {
    clearFrameState();

    // Check all keys we care about
    // GLFW keys range from 32 (space) to around 348
    static const int keysToCheck[] = {
        // Printable keys
        GLFW_KEY_SPACE, GLFW_KEY_APOSTROPHE, GLFW_KEY_COMMA, GLFW_KEY_MINUS,
        GLFW_KEY_PERIOD, GLFW_KEY_SLASH,
        GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
        GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9,
        GLFW_KEY_SEMICOLON, GLFW_KEY_EQUAL,
        GLFW_KEY_A, GLFW_KEY_B, GLFW_KEY_C, GLFW_KEY_D, GLFW_KEY_E,
        GLFW_KEY_F, GLFW_KEY_G, GLFW_KEY_H, GLFW_KEY_I, GLFW_KEY_J,
        GLFW_KEY_K, GLFW_KEY_L, GLFW_KEY_M, GLFW_KEY_N, GLFW_KEY_O,
        GLFW_KEY_P, GLFW_KEY_Q, GLFW_KEY_R, GLFW_KEY_S, GLFW_KEY_T,
        GLFW_KEY_U, GLFW_KEY_V, GLFW_KEY_W, GLFW_KEY_X, GLFW_KEY_Y,
        GLFW_KEY_Z,
        GLFW_KEY_LEFT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_RIGHT_BRACKET,
        GLFW_KEY_GRAVE_ACCENT,
        // Function keys
        GLFW_KEY_ESCAPE, GLFW_KEY_ENTER, GLFW_KEY_TAB, GLFW_KEY_BACKSPACE,
        GLFW_KEY_INSERT, GLFW_KEY_DELETE, GLFW_KEY_RIGHT, GLFW_KEY_LEFT,
        GLFW_KEY_DOWN, GLFW_KEY_UP, GLFW_KEY_PAGE_UP, GLFW_KEY_PAGE_DOWN,
        GLFW_KEY_HOME, GLFW_KEY_END,
        GLFW_KEY_F1, GLFW_KEY_F2, GLFW_KEY_F3, GLFW_KEY_F4, GLFW_KEY_F5,
        GLFW_KEY_F6, GLFW_KEY_F7, GLFW_KEY_F8, GLFW_KEY_F9, GLFW_KEY_F10,
        GLFW_KEY_F11, GLFW_KEY_F12,
        // Modifiers (tracked separately but also generate events)
        GLFW_KEY_LEFT_SHIFT, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_ALT,
        GLFW_KEY_LEFT_SUPER, GLFW_KEY_RIGHT_SHIFT, GLFW_KEY_RIGHT_CONTROL,
        GLFW_KEY_RIGHT_ALT, GLFW_KEY_RIGHT_SUPER,
    };

    for (int key : keysToCheck) {
        if (key < 0 || key >= MAX_KEYS) continue;

        const auto& state = ctx.key(key);

        // Update held state
        m_keyHeld[key] = state.held;

        // Generate press event
        if (state.pressed) {
            m_keyPressedThisFrame[key] = true;
            m_anyKeyPressed = true;
            m_lastKeyPressed = key;
            m_frameEvents.push_back(Event::keyPress(key));
        }

        // Generate release event
        if (state.released) {
            m_keyReleasedThisFrame[key] = true;
            m_frameEvents.push_back(Event::keyRelease(key));
        }
    }

    // Update modifier state
    m_shiftHeld = ctx.shiftHeld();
    m_ctrlHeld = ctx.ctrlHeld();
    m_altHeld = ctx.altHeld();
    m_superHeld = ctx.superHeld();
}

bool KeyboardIn::keyPressed(int keyCode) const {
    if (keyCode < 0 || keyCode >= MAX_KEYS) return false;
    return m_keyPressedThisFrame[keyCode];
}

bool KeyboardIn::keyHeld(int keyCode) const {
    if (keyCode < 0 || keyCode >= MAX_KEYS) return false;
    return m_keyHeld[keyCode];
}

bool KeyboardIn::keyReleased(int keyCode) const {
    if (keyCode < 0 || keyCode >= MAX_KEYS) return false;
    return m_keyReleasedThisFrame[keyCode];
}

} // namespace vivid
