#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window.h"
#include "ui/clip_editor.h"
#include "audio/vst3_host.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace vivid::input {

// The clip editor gets first crack at keys: Esc closes it; Delete/undo/select-all/tool are
// handled by on_key. Returns true when the editor consumed the key.
bool editor_key(Window& win, int key, int mods) {
    if (key == GLFW_KEY_ESCAPE && win.editor && win.editor->is_open()) { win.editor->close(); return true; }
    if (win.editor && win.editor->is_open() && win.editor->on_key(key, mods)) return true;
    return false;
}

// Scroll inside the open editor's panel routes to it (zoom/scroll). Returns true when consumed.
bool editor_scroll(Window& win, double xoff, double yoff, int mods, double mx, double my) {
    if (win.editor && win.editor->is_open() && win.editor->contains(mx, my)) {
        win.editor->on_scroll(xoff, yoff, mods, mx, my);
        return true;
    }
    return false;
}

// The clip editor is non-modal: presses inside its panel route to it (and consume); a release
// always ends any editor drag but does NOT consume (other release handling still runs).
bool editor_mouse(Window& win, int button, int action, double mx, double my, int mods) {
    if (win.editor && win.editor->is_open()) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) win.editor->on_up(mx, my);
        if (action == GLFW_PRESS && win.editor->contains(mx, my)) {
            if (button == GLFW_MOUSE_BUTTON_LEFT) win.editor->on_down(mx, my, glfwGetTime(), mods);
            return true;
        }
    }
    return false;
}

// Open the docked editor on a clip (double-click from the grid): a waveform editor for audio
// tracks, else the MIDI piano roll (with loop + ghost notes from the other tracks' same scene).
// Grows the shared dock to a comfortable editing height. Caller guards that win.editor exists.
void editor_open_clip(Window& win, App& app, int t, int sc, int tracks) {
    namespace S = vivid::session;
    // The shell draws the clip name and its location at different weights, so build them separately.
    char title[48], ident[64];
    std::snprintf(title, sizeof title, "Clip %c", 'A' + sc);
    std::snprintf(ident, sizeof ident, "%s \xC2\xB7 Scene %c", S::session_track_name(app.session, t), 'A' + sc);
    // Grow the shared dock to a comfortable editing height (the piano roll needs room); the user
    // can still resize it. Capped to 60% of the window.
    win.dock_h = std::min(std::max(win.dock_h, 320.f), win.win_h * 0.6f);
    win.editor->set_window(static_cast<float>(win.win_w), static_cast<float>(win.win_h));
    win.editor->set_dock_h(win.dock_h);   // fit the docked view to the current dock height
    if (S::session_track_is_audio(app.session, t)) {  // waveform editor
        float bins[512]; float a = 0.f, b = 1.f;
        const int nb = S::session_audio_waveform(app.session, t, sc, bins, 512);
        S::session_get_audio_trim(app.session, t, sc, &a, &b);
        const double lb = S::session_audio_loop_beats(app.session, t, sc);
        win.editor->open_audio(t, sc, title, ident, bins, nb, a, b, lb);
        // Load the clip's warp/pitch state + marker overlay (A5).
        win.editor->set_audio_shape(S::session_get_audio_warp(app.session, t, sc),
                                    S::session_get_audio_pitch(app.session, t, sc));
        float wp[256], tr[512]; double wb[256];
        const int nw = S::session_audio_get_warp_pts(app.session, t, sc, wp, 256);
        S::session_audio_get_warp_beats(app.session, t, sc, wb, 256);
        const int ntr = S::session_audio_get_transients(app.session, t, sc, tr, 512);
        win.editor->set_audio_markers(wp, wb, nw, tr, ntr);
    } else {                                          // piano-roll editor
        S::ClipNote buf[256];
        const int n = S::session_get_clip(app.session, t, sc, buf, 256);
        const double len = S::session_clip_length(app.session, t, sc);
        win.editor->open(t, sc, title, ident, buf, n, len);
        double ls = 0, le = 0;
        S::session_get_clip_loop(app.session, t, sc, &ls, &le);
        win.editor->set_loop(ls, le);
        // Ghost reference: gather the same-scene notes of the other MIDI tracks.
        std::vector<S::ClipNote> ghosts;
        for (int gt = 0; gt < tracks; ++gt) {
            if (gt == t || S::session_track_is_audio(app.session, gt)) continue;
            S::ClipNote gb[256];
            const int gn = S::session_get_clip(app.session, gt, sc, gb, 256);
            ghosts.insert(ghosts.end(), gb, gb + gn);
        }
        win.editor->set_ghost_notes(ghosts.data(), static_cast<int>(ghosts.size()));
    }
}

}  // namespace vivid::input
