#include "app/video_recorder.h"

#include "platform/av_exporter.h"
#include "transport.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace vivid {

namespace {

// Accept only an absolute path with no ".." traversal and a supported video extension. Keeps a
// scripted MCP caller (or a stray menu path) from writing outside an intended location or picking
// a container AVFoundation won't mux. Mirrors vivid-classic's is_safe_recording_path.
bool is_safe_recording_path(const std::string& p, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (p.empty())              return fail("path is empty");
    if (p[0] != '/')            return fail("path must be absolute");
    if (p.find("..") != std::string::npos) return fail("path must not contain '..'");
    auto dot = p.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? "" : p.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext != "mp4" && ext != "mov") return fail("path must end in .mp4 or .mov");
    return true;
}

}  // namespace

VideoRecorder::VideoRecorder() : exporter_(make_platform_av_exporter()) {}
VideoRecorder::VideoRecorder(std::unique_ptr<AVExporter> exporter) : exporter_(std::move(exporter)) {}
VideoRecorder::~VideoRecorder() {
    if (exporter_ && exporter_->is_recording()) stop();
}

bool VideoRecorder::start(const std::string& path, double fps, double seconds,
                          uint32_t out_w, uint32_t out_h, uint32_t sample_rate,
                          Transport& tr, std::string* err) {
    if (!exporter_) { if (err) *err = "no exporter"; return false; }
    if (exporter_->is_recording()) { if (err) *err = "already recording"; return false; }
    if (!is_safe_recording_path(path, err)) return false;

    // Lock the video size, floored to even (H.264 requires even dimensions).
    if (out_w < 2 || out_h < 2) { if (err) *err = "no output to record (render target is empty)"; return false; }
    w_ = out_w & ~1u;
    h_ = out_h & ~1u;
    fps_ = (fps > 0.0) ? fps : 60.0;
    deadline_ = (seconds > 0.0) ? seconds : 0.0;
    path_ = path;
    tr_ = &tr;

    if (!exporter_->start(path, w_, h_, fps_, sample_rate)) {
        if (err) *err = "failed to start writer (see log)";
        tr_ = nullptr;
        return false;
    }
    tr.start_recording_tap();
    std::fprintf(stderr, "[vivid] VideoRecorder: started %s (%ux%u @ %.0ffps, %s)\n",
                 path.c_str(), w_, h_, fps_, deadline_ > 0.0 ? "timed" : "manual");
    return true;
}

bool VideoRecorder::tick(const uint8_t* rgba, uint32_t w, uint32_t h, Transport& tr) {
    if (!exporter_ || !exporter_->is_recording() || stopping_) return false;

    // --- Video: append this frame (matching the locked dims, or cropped to them).
    if (rgba) {
        if (w == w_ && h == h_) {
            exporter_->write_video_frame(rgba, w_, h_);
        } else if (w >= w_ && h >= h_) {
            // Output grew or was odd → crop the top-left w_×h_ region into a scratch buffer.
            crop_scratch_.resize(static_cast<size_t>(w_) * h_ * 4);
            for (uint32_t y = 0; y < h_; ++y) {
                const uint8_t* src = rgba + static_cast<size_t>(y) * w * 4;
                uint8_t* dst = crop_scratch_.data() + static_cast<size_t>(y) * w_ * 4;
                std::copy(src, src + static_cast<size_t>(w_) * 4, dst);
            }
            exporter_->write_video_frame(crop_scratch_.data(), w_, h_);
        }
        // else: output shrank below the locked size mid-record → skip (audio holds sync).
    }

    // --- Audio: drain the recording tap and append it.
    const uint64_t avail = tr.available_recorded_samples();
    if (avail > 0) {
        audio_scratch_.resize(avail);
        const uint64_t popped = tr.pop_recorded_samples(audio_scratch_.data(), avail);
        if (popped > 0) exporter_->write_audio_samples(audio_scratch_.data(), popped, 2);
    }

    // --- Auto-stop at the deadline (export_video convenience path).
    if (deadline_ > 0.0 && exporter_->elapsed_sec() >= deadline_) {
        stop();
        return true;
    }
    return false;
}

VideoExportStatus VideoRecorder::stop() {
    if (!exporter_ || !exporter_->is_recording() || stopping_) return last_;
    stopping_ = true;   // finish() below pumps the run loop; keep a re-entrant tick/stop out

    // Drain any remaining tapped audio before finalizing so the tail isn't clipped.
    if (tr_) {
        const uint64_t avail = tr_->available_recorded_samples();
        if (avail > 0) {
            audio_scratch_.resize(avail);
            const uint64_t popped = tr_->pop_recorded_samples(audio_scratch_.data(), avail);
            if (popped > 0) exporter_->write_audio_samples(audio_scratch_.data(), popped, 2);
        }
    }

    last_ = VideoExportStatus{};
    last_.recording = false;
    last_.path = path_;
    last_.frames = exporter_->frame_count();
    last_.elapsed_sec = exporter_->elapsed_sec();
    last_.width = w_;
    last_.height = h_;

    exporter_->finish();
    if (tr_) {
        // Ph2 P3-01: report tap overruns here (main thread) instead of on the audio thread.
        if (const uint64_t ov = tr_->recording_tap_overruns(); ov > 0)
            std::fprintf(stderr, "[vivid] recording tap overran %llu time(s) — some audio was dropped during export\n",
                         static_cast<unsigned long long>(ov));
        tr_->stop_recording_tap();
    }
    tr_ = nullptr;
    stopping_ = false;
    return last_;
}

bool VideoRecorder::is_recording() const {
    return exporter_ && exporter_->is_recording();
}

VideoExportStatus VideoRecorder::status() const {
    if (exporter_ && exporter_->is_recording()) {
        VideoExportStatus s;
        s.recording = true;
        s.path = path_;
        s.frames = exporter_->frame_count();
        s.elapsed_sec = exporter_->elapsed_sec();
        s.width = w_;
        s.height = h_;
        return s;
    }
    return last_;
}

}  // namespace vivid
