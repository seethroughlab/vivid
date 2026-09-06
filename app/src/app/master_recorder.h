#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Transport;

namespace vivid {

// A snapshot of the master-record state, returned by status()/stop() and surfaced by the MCP
// master_record_status tool.
struct MasterRecordStatus {
    bool        recording = false;
    std::string path;
    uint64_t    frames = 0;          // per-channel frames written
    double      duration_sec = 0.0;
    uint32_t    sample_rate = 0;
    float       peak = 0.0f;         // max |sample| seen
    bool        clipped = false;     // peak > 1.0
    uint64_t    overruns = 0;        // tap ring overruns; 0 = a gapless capture
    std::string error;               // set when a start() failed
};

// Realtime capture of the master mix to a .wav, for recording a performance you play by hand.
//
// The offline bounce (audio_bounce.h) renders the CURRENT arming from beat 0 and cannot replay a
// timeline of scene launches, so it cannot capture a hand-performed arrangement. The only realtime
// capture that existed was the AV video export, whose audio is lossy AAC inside an .mp4 — no way to
// get a lossless master of a take.
//
// Both halves already existed: Transport's recording tap is a gapless lock-free SPSC ring of the
// final interleaved stereo master (previously used only by VideoRecorder), and the WAV encoder is
// the same ma_encoder the offline bounce uses. This is the second consumer of the tap.
//
// MAIN THREAD ONLY. The tap has ONE read cursor, so it supports one consumer at a time: start()
// refuses while a video export holds it rather than silently interleaving and corrupting both.
class MasterRecorder {
public:
    MasterRecorder() = default;
    ~MasterRecorder();
    MasterRecorder(const MasterRecorder&) = delete;
    MasterRecorder& operator=(const MasterRecorder&) = delete;

    // Begin recording the master mix to `path` (absolute, .wav). Arms the transport tap. Returns
    // false and fills *err if already recording, the path is unsafe, the sample rate is unknown, the
    // tap is already held (video export), or the file could not be opened.
    bool start(const std::string& path, Transport& tr, std::string* err = nullptr);

    // Drain whatever the audio thread has produced since the last tick and write it. No-op unless
    // recording. Call once per frame from the frame loop.
    void tick(Transport& tr);

    // Flush the remaining tap contents, close the file, disarm the tap.
    MasterRecordStatus stop(Transport& tr);

    bool is_recording() const { return recording_; }
    MasterRecordStatus status() const;

private:
    void write(const float* interleaved, uint64_t samples);

    bool               recording_ = false;
    void*              enc_ = nullptr;       // ma_encoder* (opaque: miniaudio.h stays out of this header)
    std::string        path_;
    uint32_t           sr_ = 0;
    uint64_t           frames_ = 0;
    float              peak_ = 0.0f;
    std::vector<float> scratch_;
    MasterRecordStatus last_;                // status() after a stop
};

}  // namespace vivid
