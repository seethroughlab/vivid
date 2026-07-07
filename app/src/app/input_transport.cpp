#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window.h"
#include "ui/layout.h"          // hit + transport_*_rect
#include "audio/vst3_host.h"
#include "transport.h"          // Transport::toggle_playing

namespace {
using namespace vivid::ui;      // hit, transport_play/record/metro_rect
namespace S = vivid::session;
}

namespace vivid::input {

// Top transport bar clicks: play/pause, record (M6), metronome (M6). Returns true when consumed.
bool transport_mouse(Window& win, App& app, int button, int action, double mx, double my) {
    (void)win;
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return false;
    if (app.transport && hit(transport_play_rect(), mx, my)) { app.transport->toggle_playing(); return true; }
    if (app.session && hit(transport_record_rect(), mx, my)) {
        const bool rec = S::session_is_recording(app.session);
        if (!rec && S::session_armed_track(app.session) < 0) return true;   // nothing armed — consume, no-op
        S::session_set_recording(app.session, !rec, 0.0);
        return true;
    }
    if (app.session && hit(transport_metro_rect(), mx, my)) {
        S::session_set_metronome(app.session, S::session_get_metronome(app.session) ? 0 : 1);
        return true;
    }
    return false;
}

// Transport keyboard shortcuts: Space = play/stop, R = record toggle (needs an armed track to
// start). Returns true when the key was consumed.
bool transport_key(Window& win, App& app, int key) {
    (void)win;
    if (key == GLFW_KEY_SPACE && app.transport) { app.transport->toggle_playing(); return true; }
    if (key == GLFW_KEY_R && app.session) {
        const bool rec = S::session_is_recording(app.session);
        if (rec || S::session_armed_track(app.session) >= 0) S::session_set_recording(app.session, !rec, 0.0);
        return true;
    }
    return false;
}

}  // namespace vivid::input
