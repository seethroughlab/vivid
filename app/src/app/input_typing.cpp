#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window.h"
#include "ui/clip_editor.h"
#include "audio/vst3_host.h"

#include <algorithm>
#include <cstdio>

namespace {
// Musical typing (M6.2): map a QWERTY key to a semitone slot 0..15 above the base C,
// using Ableton's layout (bottom row = white keys, upper row = sharps). -1 = not a note key.
int mt_slot(int key) {
    switch (key) {
        case GLFW_KEY_A: return 0;  case GLFW_KEY_W: return 1;
        case GLFW_KEY_S: return 2;  case GLFW_KEY_E: return 3;
        case GLFW_KEY_D: return 4;  case GLFW_KEY_F: return 5;
        case GLFW_KEY_T: return 6;  case GLFW_KEY_G: return 7;
        case GLFW_KEY_Y: return 8;  case GLFW_KEY_H: return 9;
        case GLFW_KEY_U: return 10; case GLFW_KEY_J: return 11;
        case GLFW_KEY_K: return 12; case GLFW_KEY_O: return 13;
        case GLFW_KEY_L: return 14; case GLFW_KEY_P: return 15;
        default: return -1;
    }
}
}  // namespace

namespace vivid::input {

// Musical typing (M6.2): the computer keyboard plays the armed track's instrument. Toggle with `
// (grave). Runs before the PRESS-only gate in key_callback because note-off needs the RELEASE
// event. Note/octave/velocity keys are swallowed so they don't also fire global shortcuts;
// everything else (space, etc.) still falls through (returns false).
bool typing_key(Window& win, App& app, int key, int action) {
    if (action == GLFW_PRESS && key == GLFW_KEY_GRAVE_ACCENT) {
        win.typing = !win.typing;
        std::fprintf(stderr, "[vivid] musical typing %s\n", win.typing ? "ON" : "off");
        if (!win.typing && app.session)   // silence held notes when leaving typing mode
            for (int s = 0; s < 16; ++s)
                if (win.typing_held[s]) { vivid::session::session_note_off(app.session, win.typing_held[s] - 1); win.typing_held[s] = 0; }
        return true;
    }
    if (win.typing && app.session) {
        const int slot = mt_slot(key);
        if (slot >= 0) {
            const bool step = win.editor && win.editor->is_open() && win.editor->step_mode();
            if (action == GLFW_PRESS && !win.typing_held[slot]) {
                const int pitch = std::clamp(60 + 12 * win.typing_oct + slot, 0, 127);
                vivid::session::session_note_on(app.session, pitch, win.typing_vel);
                win.typing_held[slot] = pitch + 1;
                if (step) win.editor->step_note_on(pitch, win.typing_vel);
            } else if (action == GLFW_RELEASE && win.typing_held[slot]) {
                vivid::session::session_note_off(app.session, win.typing_held[slot] - 1);
                win.typing_held[slot] = 0;
                if (step) win.editor->step_note_off();
            }
            return true;  // swallow
        }
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            if (key == GLFW_KEY_Z) { win.typing_oct = std::max(-4, win.typing_oct - 1); return true; }
            if (key == GLFW_KEY_X) { win.typing_oct = std::min( 4, win.typing_oct + 1); return true; }
            if (key == GLFW_KEY_C) { win.typing_vel = std::max(0.1f, win.typing_vel - 0.1f); return true; }
            if (key == GLFW_KEY_V) { win.typing_vel = std::min(1.0f, win.typing_vel + 0.1f); return true; }
        }
    }
    return false;
}

}  // namespace vivid::input
