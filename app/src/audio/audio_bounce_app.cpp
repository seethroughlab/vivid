#include "audio/audio_bounce.h"

#include "app/app.h"
#include "audio/vst3_host.h"   // vivid::session::session_process
#include "transport.h"

#include "miniaudio.h"         // ma_device_stop / ma_device_start

#include <vector>

namespace vivid {

bool run_audio_bounce(App& app, const BounceRequest& req, BounceResult& out, std::string* err) {
    if (!app.session)   { if (err) *err = "no session";   return false; }
    if (!app.transport) { if (err) *err = "no transport"; return false; }
    Transport& tr = *app.transport;

    // Take exclusive ownership of the session for the render. ma_device_stop() blocks until any
    // in-flight audio callback has returned, so once it returns nothing else calls session_process —
    // no race on the shared session, no hand-rolled handshake. Skip when headless (no device).
    ma_device* dev = static_cast<ma_device*>(app.audio_device);
    if (dev) ma_device_stop(dev);

    const bool okr = bounce_session_to_wav(app.session, tr, req, out, err);

    // The offline pass advanced voices/clip state from beat 0; send an all-notes-off at the live beat
    // position so realtime playback resumes clean (no notes stuck on from the bounce).
    const uint32_t sr = tr.audio_sample_rate();
    if (sr) {
        const uint32_t blk = req.block ? req.block : 1024;
        std::vector<float> flush(static_cast<size_t>(blk) * 2, 0.f);
        vivid::session::session_process(app.session, flush.data(), blk, sr,
                                        tr.bpm.load(std::memory_order_relaxed),
                                        tr.beats.load(std::memory_order_relaxed),
                                        tr.beats_per_bar.load(std::memory_order_relaxed),
                                        /*playing*/false, /*release_all*/true);
    }

    if (dev) ma_device_start(dev);
    if (okr) app.last_audio_export = out;
    return okr;
}

}  // namespace vivid
