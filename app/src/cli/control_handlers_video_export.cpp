#include "cli/control_handlers_internal.h"

#include "app/app.h"
#include "app/video_recorder.h"
#include "gpu/visual_graph.h"
#include "transport.h"

// Realtime AV video export control surface. Records the live Output render target + master audio
// mix to a signed-format-agnostic .mp4/.mov (see app/video_recorder + platform/av_exporter). Names
// are deliberately distinct from the MIDI/audio `record` tool. All handlers run on the main thread
// (process_pending), which owns the GPU device + Cocoa run loop the exporter needs.
namespace vivid {

namespace {
// Shared preflight: fetch the recorder + the graph/transport a start() needs.
bool export_ctx(const ControlCtx& c, VideoRecorder*& rec, json& e) {
    if (!c.app || !c.app->recorder) { e = err(code::kInternal, "no recorder"); return false; }
    if (!c.vgraph)    { e = err(code::kNoVgraph, "no visual graph"); return false; }
    if (!c.transport) { e = err(code::kNoTransport, "no transport"); return false; }
    rec = c.app->recorder;
    return true;
}
}  // namespace

void register_video_export_handlers(Handlers& handlers_) {
    // Begin a manual export (records until stop_video_export). {path, fps=60}
    handlers_["start_video_export"] = [](const ControlCtx& c, const json& b) -> json {
        VideoRecorder* rec = nullptr; json e;
        if (!export_ctx(c, rec, e)) return e;
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "path is required");
        const double fps = b.value("fps", 60.0);
        std::string msg;
        if (!rec->start(path, fps, 0.0, c.vgraph->rt_w(), c.vgraph->rt_h(),
                        c.transport->audio_sample_rate(), *c.transport, &msg))
            return err(code::kBadArg, msg);
        const auto st = rec->status();
        json r = ok();
        r["status"] = "recording"; r["path"] = st.path;
        r["width"] = st.width; r["height"] = st.height;
        return r;
    };

    // Finalize the current export. {} -> {ok, path, frames, duration_sec}
    handlers_["stop_video_export"] = [](const ControlCtx& c, const json&) -> json {
        if (!c.app || !c.app->recorder) return err(code::kInternal, "no recorder");
        if (!c.app->recorder->is_recording()) return err(code::kBadArg, "not recording");
        const auto st = c.app->recorder->stop();
        json r = ok();
        r["path"] = st.path; r["frames"] = st.frames; r["duration_sec"] = st.elapsed_sec;
        return r;
    };

    // Convenience for scripted capture: start with an auto-stop deadline, return immediately, then
    // poll video_export_status until recording:false. {path, seconds, fps=60}
    handlers_["export_video"] = [](const ControlCtx& c, const json& b) -> json {
        VideoRecorder* rec = nullptr; json e;
        if (!export_ctx(c, rec, e)) return e;
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "path is required");
        const double seconds = b.value("seconds", 0.0);
        if (seconds <= 0.0) return err(code::kBadArg, "seconds must be > 0");
        const double fps = b.value("fps", 60.0);
        std::string msg;
        if (!rec->start(path, fps, seconds, c.vgraph->rt_w(), c.vgraph->rt_h(),
                        c.transport->audio_sample_rate(), *c.transport, &msg))
            return err(code::kBadArg, msg);
        const auto st = rec->status();
        json r = ok();
        r["status"] = "recording"; r["path"] = st.path; r["seconds"] = seconds;
        r["width"] = st.width; r["height"] = st.height;
        return r;
    };

    // Poll the current/last export. {} -> {ok, recording, path, frames, elapsed_sec, width, height}
    handlers_["video_export_status"] = [](const ControlCtx& c, const json&) -> json {
        if (!c.app || !c.app->recorder) return err(code::kInternal, "no recorder");
        const auto st = c.app->recorder->status();
        json r = ok();
        r["recording"] = st.recording; r["path"] = st.path;
        r["frames"] = st.frames; r["elapsed_sec"] = st.elapsed_sec;
        r["width"] = st.width; r["height"] = st.height;
        return r;
    };
}

}  // namespace vivid
