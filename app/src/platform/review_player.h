#pragma once
// ADR-0064 — the REVIEW media player: plays a rendered candidate/baseline (.mp4/.mov, video + audio)
// for audition in the Review workspace. It is deliberately OUTSIDE the engine: audio goes to the
// system default output through the platform media stack (not miniaudio, not the session mix), and
// video frames are pulled here and uploaded to a 2D texture by the caller. So reviewing never touches
// the Create document, the audio graph, or the visual graph (ADR-0062 §3: audition does not modify
// the preferred version), and rendered reviews stay playable with no runner and no engine involvement.
//
// The concrete implementation is AVFoundation (review_player.mm, macOS) or a no-op stub
// (review_player_stub.cpp) — pick one with make_platform_review_player(). Pure interface so the A/B
// audition logic (ui/review_audition.h) is unit-testable against a mock without AVFoundation.
//
// Threading: main thread only (the frame loop drives poll_frame()).
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vivid {

// A decoded video frame: tightly described BGRA8 rows at media time `time_sec`. The pointer is valid
// until the next poll_frame()/close() on the same player.
struct ReviewFrame {
    const uint8_t* bgra          = nullptr;
    uint32_t       width         = 0;
    uint32_t       height        = 0;
    uint32_t       bytes_per_row = 0;
    double         time_sec      = 0.0;
};

class ReviewPlayer {
public:
    virtual ~ReviewPlayer() = default;

    // Begin loading `path`. Returns false only for an immediately invalid request; loading is
    // asynchronous — is_ready() flips when the asset's duration + tracks are known, or error() is set.
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;
    virtual bool is_open()  const = 0;
    virtual bool is_ready() const = 0;
    virtual std::string error() const = 0;   // non-empty when loading failed
    virtual const std::string& path() const = 0;

    virtual double duration()     const = 0;   // seconds; 0 until ready
    virtual double current_time() const = 0;   // media seconds
    virtual bool   playing()      const = 0;
    virtual bool   at_end()       const = 0;   // played through to the end (stopped there)

    virtual void play()  = 0;
    virtual void pause() = 0;
    virtual void seek(double sec) = 0;   // exact (frame-accurate) seek, paused or playing

    // Start playback from media time `media_sec` exactly at host time `host_time_sec` (see
    // review_host_time_now()). Several players given the SAME host time start in lockstep — the
    // synchronized A/B of ADR-0064 §3. The host time should be a little in the future (~50 ms) so
    // every player can preroll.
    virtual void play_at(double media_sec, double host_time_sec) = 0;

    // Audition-only level. `gain` is linear (1 = the file as rendered). It never alters the authored
    // mix — it exists for the labeled, reversible listening-level match (ADR-0064 §3).
    virtual void  set_gain(float gain) = 0;
    virtual float gain() const = 0;
    virtual void  set_muted(bool m) = 0;
    virtual bool  muted() const = 0;

    // Pull the newest video frame due at the current media time, if one arrived since the last call.
    // Returns false when nothing new (the caller keeps showing its last texture).
    virtual bool poll_frame(ReviewFrame& out) = 0;

    // Video dimensions once ready (0 until then; 0 for an audio-only file).
    virtual uint32_t video_width()  const = 0;
    virtual uint32_t video_height() const = 0;
};

// The platform-appropriate concrete player (AVFoundation on macOS, a stub elsewhere).
std::unique_ptr<ReviewPlayer> make_platform_review_player();

// The host clock play_at() is expressed in (seconds; CoreMedia's host time clock on macOS). The stub
// uses a steady clock so the audition logic still has a consistent time base off macOS.
double review_host_time_now();

}  // namespace vivid
