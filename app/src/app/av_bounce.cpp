#include "app/av_bounce.h"

#include "audio/vst3_host.h"        // vivid::session::session_process
#include "transport.h"
#include "platform/av_exporter.h"   // AVExporter (pure interface — no AVFoundation, no GPU)

#include <algorithm>
#include <cmath>
#include <vector>

// The PURE, RESUMABLE offline AV loop: audio via session_process + video via an injected AvFrameSource,
// muxed with deterministic PTS. No GPU, no AVFoundation, no miniaudio — so it links (and is fully unit-
// testable) in the AUDIO_ENGINE test tier with a mock frame source + mock exporter. The GPU/device
// orchestration (VisualGraphFrameSource + the async runner) lives in av_bounce_app.cpp.
namespace vivid {

bool AvExportJob::begin(vivid::session::Session* session, const Transport& tr, AvFrameSource& src,
                        AVExporter& exporter, AvReactiveFeed* feed, const AvBounceRequest& req,
                        std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (!session) return fail("no session");
    constexpr auto rel = std::memory_order_relaxed;
    const uint32_t sr = tr.audio_sample_rate();
    if (sr == 0) return fail("audio sample rate unknown (audio device not initialized)");
    const double bpm = tr.bpm.load(rel);
    const int    bpb = tr.beats_per_bar.load(rel);

    double seconds = req.seconds;
    if (seconds <= 0.0 && req.bars > 0.0 && bpm > 0.0) seconds = req.bars * bpb * 60.0 / bpm;
    if (seconds <= 0.0) return fail("seconds (or bars, with a running tempo) must be > 0");

    session_ = session; src_ = &src; exporter_ = &exporter; feed_ = feed;
    sr_ = sr; bpb_ = static_cast<uint32_t>(bpb > 0 ? bpb : 4); bpm_ = bpm; bps_ = bpm / 60.0;
    block_ = req.block ? req.block : 1024;
    fps_   = req.fps > 0.0 ? req.fps : 60.0;
    w_ = src.width(); h_ = src.height();
    total_frames_ = static_cast<uint64_t>(std::llround(seconds * fps_));
    if (total_frames_ == 0) return fail("resolved length is zero frames");
    frame_i_ = 0; sample_pos_ = 0; peak_ = 0.f;
    path_ = req.path;
    return true;
}

bool AvExportJob::step(uint32_t max_frames) {
    for (uint32_t k = 0; k < max_frames && frame_i_ < total_frames_; ++k, ++frame_i_) {
        // Sample-accurate frame boundary: llround telescopes so Σ n_frame == total audio, no drift
        // (handles 44100/60 = 735/736 alternation).
        const uint64_t audio_end = static_cast<uint64_t>(std::llround(static_cast<double>(frame_i_ + 1) * sr_ / fps_));
        const uint32_t n_frame   = static_cast<uint32_t>(audio_end - sample_pos_);
        const double   beats_at  = static_cast<double>(sample_pos_) / sr_ * bps_;

        // Render this frame's audio (sub-blocked to <= block, advancing beats per sub-block like the WAV bounce).
        pcm_.assign(static_cast<size_t>(n_frame) * 2, 0.f);
        for (uint32_t done = 0; done < n_frame; ) {
            const uint32_t nb = std::min(block_, n_frame - done);
            const double   b  = beats_at + static_cast<double>(done) / sr_ * bps_;
            vivid::session::session_process(session_, pcm_.data() + static_cast<size_t>(done) * 2, nb,
                                            sr_, bpm_, b, static_cast<int>(bpb_),
                                            /*playing*/true, /*release_all*/false);
            done += nb;
        }
        for (float s : pcm_) peak_ = std::max(peak_, std::fabs(s));

        // Reactive feed (C2): the visual reacts to THIS frame's audio; must run before the render.
        if (feed_) feed_->feed(pcm_.data(), n_frame, beats_at);

        // Visual frame, time locked to the audio position.
        if (!src_->render(static_cast<double>(sample_pos_) / sr_, beats_at, rgba_)) {
            // Give up cleanly: mark done so the caller finalizes; leave the file valid up to here.
            frame_i_ = total_frames_;
            break;
        }

        // Deterministic PTS: video on the even i/fps grid, audio at its exact sample position.
        exporter_->write_video_frame(rgba_.data(), w_, h_, static_cast<double>(frame_i_) / fps_);
        exporter_->write_audio_samples(pcm_.data(), static_cast<uint64_t>(n_frame) * 2, 2,
                                       static_cast<double>(sample_pos_) / sr_);
        sample_pos_ = audio_end;
    }

    if (done()) {
        result_.path         = path_;
        result_.frames       = frame_i_;
        result_.audio_frames = sample_pos_;
        result_.duration_sec = static_cast<double>(frame_i_) / fps_;
        result_.peak         = peak_;
        result_.clipped      = peak_ > 1.0f;
    }
    return !done();
}

bool av_bounce_run(vivid::session::Session* session, const Transport& tr, AvFrameSource& src,
                   AVExporter& exporter, AvReactiveFeed* feed,
                   const AvBounceRequest& req, AvBounceResult& out, std::string* err) {
    AvExportJob job;
    if (!job.begin(session, tr, src, exporter, feed, req, err)) return false;
    while (job.step(1u << 20)) { /* one shot */ }
    out = job.result();
    return true;
}

}  // namespace vivid
