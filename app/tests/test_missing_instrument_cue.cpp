// UX Ph6 F2 (missing-instrument cue): a demo opened without its instrument plugins used to degrade the
// affected tracks to silence with only a stderr line. The session now records the DISPLAY NAME of every
// instrument that failed to resolve — a catalog miss that fell back to a silent placeholder, OR an async
// CLAP instrument whose load terminally failed — so the GUI can surface ONE toast naming the plugins to
// install. This covers the session-layer accumulator + the async CLAP-failure path directly (the
// persist.cpp placeholder path calls the same tested primitives); the toast itself is GUI-only.
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"
#include "gpu/op_runtime.h"
#include "test_helpers.h"

#include <string>
#include <thread>
#include <chrono>

using namespace vivid::session;
using namespace vivid::test;

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    const uint32_t sr = 48000;

    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);

    // 1) The accumulator primitives: note, dedup by name, indexed read, clear.
    CHECK(session_unresolved_instrument_count(s) == 0);
    session_note_unresolved_instrument(s, "Cassette Drums");
    session_note_unresolved_instrument(s, "Surge XT");
    session_note_unresolved_instrument(s, "Surge XT");        // duplicate — bloom points 3 tracks at it
    session_note_unresolved_instrument(s, "");                // empty ignored
    session_note_unresolved_instrument(s, nullptr);           // null ignored
    CHECK(session_unresolved_instrument_count(s) == 2);       // deduped to the two distinct plugins
    CHECK(std::string(session_unresolved_instrument_name(s, 0)) == "Cassette Drums");
    CHECK(std::string(session_unresolved_instrument_name(s, 1)) == "Surge XT");
    CHECK(std::string(session_unresolved_instrument_name(s, 99)) == "");   // out of range -> ""
    session_clear_unresolved_instruments(s);
    CHECK(session_unresolved_instrument_count(s) == 0);

    // 2) The async CLAP-instrument failure path: a track requests an instrument at a path that does not
    //    exist on this machine (exactly a demo whose Surge XT isn't installed). The load fails in the
    //    background loader, and poll records the friendly name derived from the .clap file stem.
    const int t = session_add_graph_track(s, "lead");
    CHECK(t >= 0);
    CHECK(session_request_track_clap_instrument_state(s, t, "/nonexistent/Bogus Synth.clap", "") == 1);
    CHECK(session_plugin_loads_pending(s) == 1);

    for (int i = 0; i < 2000 && session_plugin_loads_pending(s) > 0; ++i) {   // ~10s budget; a bad path fails fast
        session_poll_plugin_loads(s);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(session_plugin_loads_pending(s) == 0);              // the failed load settled
    CHECK(session_unresolved_instrument_count(s) == 1);       // and was tallied as a missing instrument
    CHECK(std::string(session_unresolved_instrument_name(s, 0)) == "Bogus Synth");   // ".clap" stem, not the path

    session_destroy(s);
    return summary("test_missing_instrument_cue");
}
