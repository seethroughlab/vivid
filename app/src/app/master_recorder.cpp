#include "app/master_recorder.h"

#include "audio/audio_bounce.h"   // is_safe_wav_path (one rule for every .wav writer)
#include "transport.h"

#include "miniaudio.h"            // ma_encoder (dr_wav-backed WAV writer)

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid {

MasterRecorder::~MasterRecorder() {
    if (enc_) {   // a crash/teardown mid-record still closes the file so it is playable
        ma_encoder_uninit(static_cast<ma_encoder*>(enc_));
        delete static_cast<ma_encoder*>(enc_);
        enc_ = nullptr;
    }
}

bool MasterRecorder::start(const std::string& path, Transport& tr, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (recording_) return fail("already recording the master mix");
    if (!is_safe_wav_path(path, err)) return false;

    const uint32_t sr = tr.audio_sample_rate();
    if (sr == 0) return fail("audio sample rate unknown (audio device not initialized)");

    // The tap has a single read cursor, so exactly one consumer may hold it. If a video export is
    // already draining it, two readers would each get a fraction of the stream and both files would
    // be gap-ridden. Refuse and say who has it, rather than producing two broken recordings.
    if (tr.recording_tap_active())
        return fail("the master tap is already in use (a video export is recording) — stop it first");

    auto* enc = new ma_encoder();
    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, sr);
    if (ma_encoder_init_file(path.c_str(), &cfg, enc) != MA_SUCCESS) {
        delete enc;
        return fail("could not open output file for writing: " + path);
    }

    enc_ = enc;
    path_ = path;
    sr_ = sr;
    frames_ = 0;
    peak_ = 0.0f;
    recording_ = true;
    tr.start_recording_tap();
    std::fprintf(stderr, "[vivid] MasterRecorder: started %s (%u Hz stereo)\n", path.c_str(), sr);
    return true;
}

void MasterRecorder::write(const float* interleaved, uint64_t samples) {
    if (!enc_ || samples == 0) return;
    for (uint64_t i = 0; i < samples; ++i) peak_ = std::max(peak_, std::fabs(interleaved[i]));
    ma_uint64 wrote = 0;
    ma_encoder_write_pcm_frames(static_cast<ma_encoder*>(enc_), interleaved, samples / 2, &wrote);
    frames_ += wrote;
}

void MasterRecorder::tick(Transport& tr) {
    if (!recording_) return;
    const uint64_t avail = tr.available_recorded_samples();
    if (avail == 0) return;
    scratch_.resize(avail);
    const uint64_t got = tr.pop_recorded_samples(scratch_.data(), avail);
    write(scratch_.data(), got);
}

MasterRecordStatus MasterRecorder::stop(Transport& tr) {
    if (!recording_) return last_;

    // Drain what the audio thread produced since the last tick BEFORE disarming, so the tail of the
    // take is not clipped off by however far into the frame the stop landed.
    tick(tr);
    tr.stop_recording_tap();
    tick(tr);   // and anything the callback wrote between that drain and the disarm

    if (enc_) {
        ma_encoder_uninit(static_cast<ma_encoder*>(enc_));
        delete static_cast<ma_encoder*>(enc_);
        enc_ = nullptr;
    }

    last_ = status();          // while recording_ is still true — status() short-circuits to last_ otherwise
    last_.recording = false;
    recording_ = false;
    // The tap counts blocks the audio thread had to drop because the main thread did not drain fast
    // enough. Report it: a take with a silent gap in it is worse than one you know is imperfect.
    last_.overruns = tr.recording_tap_overruns();
    if (last_.overruns > 0)
        std::fprintf(stderr, "[vivid] MasterRecorder: %llu tap overrun(s) — the capture has gaps\n",
                     static_cast<unsigned long long>(last_.overruns));
    std::fprintf(stderr, "[vivid] MasterRecorder: wrote %s (%.2fs, peak %.3f)\n",
                 last_.path.c_str(), last_.duration_sec, last_.peak);
    return last_;
}

MasterRecordStatus MasterRecorder::status() const {
    if (!recording_) return last_;
    MasterRecordStatus s;
    s.recording    = true;
    s.path         = path_;
    s.frames       = frames_;
    s.sample_rate  = sr_;
    s.duration_sec = sr_ ? static_cast<double>(frames_) / static_cast<double>(sr_) : 0.0;
    s.peak         = peak_;
    s.clipped      = peak_ > 1.0f;
    return s;
}

}  // namespace vivid
