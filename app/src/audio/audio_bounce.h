#pragma once
// ADR-0032 (decision #5): offline master-mix bounce to a .wav file. Renders the session's master
// output through the SAME session graph + transport semantics as realtime playback, but OFFLINE —
// faster than realtime, driven by a hand-advanced beat clock, no audio device (this is exactly what
// the headless test_session_executor already does via its render_span helper). The deterministic
// offline render is the ADR's "simpler proof of graph/transport correctness".
#include <cstdint>
#include <string>

struct Transport;
namespace vivid { struct App; }
namespace vivid::session { struct Session; }

namespace vivid {

// What to render. Provide `seconds` (primary) OR `bars` (derived from the transport's bpm +
// beats_per_bar). `block` is the render block size (frames); 0 => a sensible default.
struct BounceRequest {
    std::string path;         // absolute, ends in .wav
    double      seconds = 0.0;
    double      bars    = 0.0;
    uint32_t    block   = 0;  // 0 => default (1024)
};

// The outcome of a bounce. `path` empty => no bounce has run yet (used for audio_export_status).
struct BounceResult {
    std::string path;
    uint64_t    frames       = 0;
    double      duration_sec = 0.0;
    float       peak         = 0.0f;   // max |sample| across the render
    bool        clipped      = false;  // peak > 1.0 (would clip at 0 dBFS on export/playback)
};

// Pure, device-free render → WAV. The CALLER guarantees exclusive ownership of `session` for the
// duration of the call (nothing else may call session_process concurrently). Reads sample rate /
// bpm / beats_per_bar from `tr` but does NOT mutate the transport (renders from a LOCAL beat 0).
// Returns false and fills `err` on a bad request or a file-open/encode failure.
bool bounce_session_to_wav(vivid::session::Session* session, const Transport& tr,
                           const BounceRequest& req, BounceResult& out, std::string* err);

// Orchestration for the live app (main thread only): pause the audio device so the RT callback can't
// race the render on the shared session, run bounce_session_to_wav, send an all-notes-off to reset
// voices the offline pass advanced, then resume the device. On success stores the result in
// App::last_audio_export. Skips the pause/resume when no device is attached (headless).
bool run_audio_bounce(App& app, const BounceRequest& req, BounceResult& out, std::string* err);

}  // namespace vivid
