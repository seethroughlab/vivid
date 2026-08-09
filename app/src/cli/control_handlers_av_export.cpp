#include "cli/control_handlers_internal.h"

#include "app/app.h"
#include "app/av_bounce.h"

// ADR-0032 Phase C: deterministic offline AUDIOVISUAL export. ASYNC — export_av kicks off a job and
// returns immediately (a full render blocks the main thread for MINUTES if run synchronously, freezing the
// app and blowing the control server's 10 s reply window); the frame loop renders one frame per tick and
// av_export_status reports progress. Poll av_export_status until done. Runs on the main thread, which owns
// the GPU device + the session the render takes over while the audio device is paused for the job.
namespace vivid {

void register_av_export_handlers(Handlers& handlers_) {
    // {path, seconds?, bars?, fps=60, block?} -> {ok, started} — kicks off the async render.
    handlers_["export_av"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app)       return err(code::kInternal, "no app");
        if (!c.session)   return err(code::kInternal, "no session");
        if (!c.transport) return err(code::kNoTransport, "no transport");
        AvBounceRequest req;
        req.path    = b.value("path", std::string());
        req.seconds = b.value("seconds", 0.0);
        req.bars    = b.value("bars", 0.0);
        req.fps     = b.value("fps", 60.0);
        req.block   = static_cast<uint32_t>(b.value("block", 0));
        if (req.path.empty()) return err(code::kBadArg, "path is required");
        if (req.seconds <= 0.0 && req.bars <= 0.0)
            return err(code::kBadArg, "seconds (or bars) is required and must be > 0");
        std::string e;
        if (!av_export_start(*c.app, c.window, req, &e)) return err(code::kBadArg, e);   // c.window may be null (headless → C1 fallback)
        json r = ok();
        r["started"] = true;
        r["path"]    = req.path;
        return r;
    };

    // {} -> {ok, active, frames_done, total_frames, done, path, frames, audio_frames, duration_sec, peak,
    // clipped}. `active` = a render is in flight; `done` = a completed export exists (last result). Poll
    // until active is false, then read the last-result fields.
    handlers_["av_export_status"] = [](const ControlCtx& c, const json&) -> json {
        if (!c.app) return err(code::kInternal, "no app");
        json r = ok();
        uint64_t fd = 0, tf = 0;
        r["active"]       = av_export_progress(*c.app, fd, tf);
        r["frames_done"]  = fd;
        r["total_frames"] = tf;
        const AvBounceResult& st = c.app->last_av_export;
        r["done"]         = !st.path.empty();
        r["path"]         = st.path;
        r["frames"]       = st.frames;
        r["audio_frames"] = st.audio_frames;
        r["duration_sec"] = st.duration_sec;
        r["peak"]         = st.peak;
        r["clipped"]      = st.clipped;
        return r;
    };
}

}  // namespace vivid
