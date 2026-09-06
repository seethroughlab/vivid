#include "cli/control_handlers_internal.h"

#include "app/app.h"
#include "audio/audio_bounce.h"
#include "app/master_recorder.h"   // realtime master-mix capture (start/stop/status below)
#include "transport.h"

// ADR-0032 (decision #5): offline master-mix bounce to WAV. Distinct from the realtime AV
// video-export family — this renders the audio graph OFFLINE (device paused, faster than realtime,
// deterministic) and writes a .wav. Synchronous: export_audio returns the finished result; the
// status handler exists for parity with video_export_status + scripted polling. Runs on the main
// thread (process_pending), which owns the session the render takes over while the device is paused.
namespace vivid {

void register_audio_export_handlers(Handlers& handlers_) {
    // {path, seconds?, bars?, block?} -> {ok, path, frames, duration_sec, peak, clipped}
    handlers_["export_audio"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app)       return err(code::kInternal, "no app");
        if (!c.session)   return err(code::kInternal, "no session");
        if (!c.transport) return err(code::kNoTransport, "no transport");
        BounceRequest req;
        req.path    = b.value("path", std::string());
        req.seconds = b.value("seconds", 0.0);
        req.bars    = b.value("bars", 0.0);
        req.block   = static_cast<uint32_t>(b.value("block", 0));
        if (req.path.empty()) return err(code::kBadArg, "path is required");
        if (req.seconds <= 0.0 && req.bars <= 0.0)
            return err(code::kBadArg, "seconds (or bars) is required and must be > 0");
        BounceResult res; std::string e;
        if (!run_audio_bounce(*c.app, req, res, &e)) return err(code::kBadArg, e);
        if (res.clipped)
            VLOG_WARN(*c.app, "audio export clipped: peak %.3f (>0 dBFS): %s",
                      static_cast<double>(res.peak), res.path.c_str());
        json r = ok();
        r["path"]         = res.path;
        r["frames"]       = res.frames;
        r["duration_sec"] = res.duration_sec;
        r["peak"]         = res.peak;
        r["clipped"]      = res.clipped;
        return r;
    };

    // {} -> {ok, done, path, frames, duration_sec, peak, clipped}. `done` is false until the first
    // bounce completes this session.
    handlers_["audio_export_status"] = [](const ControlCtx& c, const json&) -> json {
        if (!c.app) return err(code::kInternal, "no app");
        const BounceResult& st = c.app->last_audio_export;
        json r = ok();
        r["done"]         = !st.path.empty();
        r["path"]         = st.path;
        r["frames"]       = st.frames;
        r["duration_sec"] = st.duration_sec;
        r["peak"]         = st.peak;
        r["clipped"]      = st.clipped;
        return r;
    };

    // ---------------- realtime master capture ----------------
    // The offline bounce above renders the CURRENT arming from beat 0 — it cannot replay a timeline
    // of scene launches, so it cannot capture an arrangement performed by hand. The only realtime
    // capture that existed was the AV video export, whose audio is lossy AAC inside an .mp4. These
    // three record the live master mix to a lossless .wav for as long as you play.
    auto master_status_json = [](const MasterRecordStatus& st) {
        json r = ok();
        r["recording"]    = st.recording;
        r["path"]         = st.path;
        r["frames"]       = st.frames;
        r["duration_sec"] = st.duration_sec;
        r["sample_rate"]  = st.sample_rate;
        r["peak"]         = st.peak;
        r["clipped"]      = st.clipped;
        r["overruns"]     = st.overruns;   // 0 = gapless; >0 means blocks were dropped
        return r;
    };

    handlers_["start_master_record"] = [master_status_json](const ControlCtx& c, const json& b) -> json {
        if (!c.app)       return err(code::kInternal, "no app");
        if (!c.transport) return err(code::kNoTransport, "no transport");
        if (!c.app->master_rec) return err(code::kInternal, "master recorder unavailable");
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "path is required");
        std::string e;
        if (!c.app->master_rec->start(path, *c.transport, &e)) return err(code::kBadArg, e);
        return master_status_json(c.app->master_rec->status());
    };

    handlers_["stop_master_record"] = [master_status_json](const ControlCtx& c, const json&) -> json {
        if (!c.app)       return err(code::kInternal, "no app");
        if (!c.transport) return err(code::kNoTransport, "no transport");
        if (!c.app->master_rec) return err(code::kInternal, "master recorder unavailable");
        if (!c.app->master_rec->is_recording()) return err(code::kBadArg, "not recording");
        const MasterRecordStatus st = c.app->master_rec->stop(*c.transport);
        if (st.clipped)
            VLOG_WARN(*c.app, "master recording clipped: peak %.3f (>0 dBFS): %s",
                      static_cast<double>(st.peak), st.path.c_str());
        if (st.overruns > 0)
            VLOG_WARN(*c.app, "master recording dropped %llu block(s) — the capture has gaps",
                      static_cast<unsigned long long>(st.overruns));
        return master_status_json(st);
    };

    handlers_["master_record_status"] = [master_status_json](const ControlCtx& c, const json&) -> json {
        if (!c.app) return err(code::kInternal, "no app");
        if (!c.app->master_rec) return err(code::kInternal, "master recorder unavailable");
        return master_status_json(c.app->master_rec->status());
    };
}

}  // namespace vivid
