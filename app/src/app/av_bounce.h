#pragma once
// ADR-0032 Phase C (decision #5, AV-half): deterministic offline AUDIOVISUAL export. Renders the visual
// graph OFFLINE, locked to the SAME synthetic clock the audio bounce advances (audio_bounce.cpp), and
// muxes H.264 video + AAC audio with SAMPLE-ACCURATE PTS — so the exported music video is deterministic,
// unlike the realtime wall-clock capture (video_recorder.cpp). Synchronous, device-paused, faster than
// realtime. Inherits the audio bounce's semantics: renders the CURRENT arming from a LOCAL beat 0 (not a
// scene-launch timeline). The metronome click + movie-audio fallback live in the RT callback, not
// session_process, so they are absent from the export (as with the WAV bounce).
#include <cstdint>
#include <string>
#include <vector>

struct Transport;
namespace vivid { struct App; struct Window; }
namespace vivid::session { struct Session; }

namespace vivid {
class AVExporter;

// What to render. `seconds` (primary) OR `bars` (derived from the transport tempo). `fps` = video frame
// rate. `block` caps the per-frame audio sub-block (frames); 0 => 1024.
struct AvBounceRequest {
    std::string path;          // absolute, ends in .mp4 or .mov
    double      seconds = 0.0;
    double      bars    = 0.0;
    double      fps     = 60.0;
    uint32_t    block   = 0;
};

struct AvBounceResult {
    std::string path;
    uint64_t    frames        = 0;   // video frames written
    uint64_t    audio_frames  = 0;   // audio frames (stereo pairs) written
    double      duration_sec  = 0.0;
    float       peak          = 0.0f;
    bool        clipped       = false;
};

// The visual-frame producer, abstracted so the loop is unit-testable without a real GPU. `render`
// advances the visual graph to `time_sec` + `beats` and returns a tightly-packed RGBA8 frame (top-left
// origin) of exactly width()×height() (both even, for H.264).
struct AvFrameSource {
    virtual ~AvFrameSource() = default;
    virtual uint32_t width()  const = 0;
    virtual uint32_t height() const = 0;
    virtual bool render(double time_sec, double beats, std::vector<uint8_t>& rgba) = 0;
};

// Per-frame reactive-input feed (Phase C2). Null in C1. Called with the frame's rendered PCM + synthetic
// beats BEFORE the visual render, so audio-reactive visuals key on the audio being muxed for this frame.
struct AvReactiveFeed {
    virtual ~AvReactiveFeed() = default;
    virtual void feed(const float* pcm_interleaved, uint32_t frames, double beats) = 0;
};

// A RESUMABLE, device-free offline AV render. Drives audio via session_process at a synthetic clock
// (beats from a LOCAL beat 0), produces one video frame per output frame from `src`, and muxes both into
// `exporter` (already start()ed + begin_offline()) with deterministic PTS (video = i/fps, audio =
// sample_pos/sr). `step()` renders a bounded batch of frames so the caller can drive it across app
// frame-loop ticks WITHOUT freezing the main thread (a full 30 s render blocks for minutes if run in one
// shot). The CALLER owns exclusive `session` access (device paused for the job's lifetime). `feed` may be
// null (C1). GPU/AVFoundation-free — testable with a mock src + exporter.
class AvExportJob {
public:
    // Resolve the request + start; false + err on a bad request (nothing rendered). References must
    // outlive the job. The exporter must already be start()ed.
    bool begin(vivid::session::Session* session, const Transport& tr, AvFrameSource& src,
               AVExporter& exporter, AvReactiveFeed* feed, const AvBounceRequest& req, std::string* err);
    // Render up to `max_frames` output frames. Returns true while frames remain (call again next tick).
    bool step(uint32_t max_frames);
    bool     done()         const { return frame_i_ >= total_frames_; }
    uint64_t frames_done()  const { return frame_i_; }
    uint64_t total_frames() const { return total_frames_; }
    const AvBounceResult& result() const { return result_; }   // finalized once done()

private:
    vivid::session::Session* session_ = nullptr;
    AvFrameSource*   src_ = nullptr;
    AVExporter*      exporter_ = nullptr;
    AvReactiveFeed*  feed_ = nullptr;
    uint32_t sr_ = 0, bpb_ = 4, block_ = 1024, w_ = 0, h_ = 0;
    double   bpm_ = 120.0, fps_ = 60.0, bps_ = 2.0;
    uint64_t total_frames_ = 0, frame_i_ = 0, sample_pos_ = 0;
    float    peak_ = 0.f;
    std::string path_;
    std::vector<uint8_t> rgba_;
    std::vector<float>   pcm_;
    AvBounceResult result_;
};

// Convenience: run a whole export to completion in one call (steps until done). Used by the headless test;
// the live app drives an AvExportJob across frame-loop ticks instead (see av_export_start / av_export_tick).
bool av_bounce_run(vivid::session::Session* session, const Transport& tr, AvFrameSource& src,
                   AVExporter& exporter, AvReactiveFeed* feed,
                   const AvBounceRequest& req, AvBounceResult& out, std::string* err);

// App orchestration (main thread only), ASYNC so a long render never freezes the app. av_export_start
// validates, pauses the audio device (so the RT callback can't race the render on the shared session),
// builds a VisualGraphFrameSource over app.vgraph + the platform exporter, and stores an active job on
// App::av_export. av_export_tick — called once per frame-loop tick — renders a batch; when the job
// finishes it finalizes the file, stores App::last_av_export, sends an all-notes-off, resumes the device,
// and returns AvTickDone (else AvTickRunning / AvTickIdle). av_export_active reports whether a job is live.
// `win` (Phase C2) enables offline reactivity: the job feeds computed master metering + synthetic beats
// into the transport atomics and calls publish_bridge_sources(app, *win) per frame, so audio-reactive
// visuals key on the muxed audio. Null (headless control caller) → time/beat-synced only, master-reactive
// stale (the C1 fallback).
enum class AvTick { Idle, Running, Done };
bool av_export_start(App& app, Window* win, const AvBounceRequest& req, std::string* err);
AvTick av_export_tick(App& app);
bool av_export_active(const App& app);
// Progress of the active job (frames rendered / total). Returns false when no job is running.
bool av_export_progress(const App& app, uint64_t& frames_done, uint64_t& total_frames);

}  // namespace vivid
