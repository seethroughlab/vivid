#include "cli/control_handlers_audio_domains.h"
#include "cli/control_handlers_internal.h"
#include "cli/audio_analysis_tools.h"   // load_pcm_file (reference decode)

#include "app/app.h"
#include "audio/music_eval.h"
#include "transport.h"

#include <cstdint>
#include <string>
#include <vector>

// ADR-0026: the in-app music-eval control surface. Handlers CAPTURE the live master window (fast, on
// the UI thread) then hand off to MusicEval, which runs the Gemini call ASYNC and returns a job id —
// so the frame loop never blocks on the network. Fail-closed: with no key configured, evaluate/compare
// return an error rather than a fabricated verdict. There is no fake stub.
namespace vivid {

namespace {
// Grab a window of the live master mix as a PCM16 WAV. Returns false + fills `e` on failure.
bool capture_master_wav(const ControlCtx& c, double window_s, std::vector<uint8_t>& wav, json& e) {
    if (!c.transport) { e = err(code::kInternal, "no transport"); return false; }
    if (window_s <= 0.0 || window_s > 30.0) { e = err(code::kBadArg, "window_seconds must be 0 < w <= 30"); return false; }
    std::vector<float> L, R; uint32_t sr = 0;
    if (c.transport->capture_snapshot(window_s, L, R, &sr) == 0 || sr == 0) {
        e = err(code::kBadArg, "no audio captured — is something playing?"); return false;
    }
    wav = pcm16_wav_from_planar(L, R, sr);
    return true;
}
}  // namespace

void register_music_eval_handlers(Handlers& handlers_) {
    handlers_["configure_music_eval_backend"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app) return err(code::kInternal, "no app context");
        const std::string backend = b.value("backend", std::string("gemini"));
        if (backend != "gemini") return err(code::kBadArg, "only 'gemini' is supported");
        c.app->music_eval.configure(b.value("api_key", std::string()), b.value("model", std::string()));
        return c.app->music_eval.status();   // {ok:true, backend, ready, has_key, model}
    };

    handlers_["music_eval_status"] = [](const ControlCtx& c, const json&) -> json {
        if (!c.app) return err(code::kInternal, "no app context");
        return c.app->music_eval.status();
    };

    // Start an async caption/theory/reasoning evaluation of the live master output. Poll music_eval_result.
    handlers_["evaluate_audio_musically"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app) return err(code::kInternal, "no app context");
        if (!c.app->music_eval.has_key())
            return err(code::kBadArg, "no_evaluator: no Gemini key — call configure_music_eval_backend first");
        std::vector<uint8_t> wav; json e;
        if (!capture_master_wav(c, b.value("window_seconds", 20.0), wav, e)) return e;
        const int id = c.app->music_eval.start_eval(std::move(wav), b.value("mode", std::string("caption")));
        if (id < 0) return err(code::kBadArg, "no_evaluator: no Gemini key");
        json r = ok(); r["job_id"] = id; r["status"] = "pending"; return r;
    };

    // Start an async compare of the live output against a free-text intent and/or a reference clip.
    handlers_["compare_audio_to_intent"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app) return err(code::kInternal, "no app context");
        if (!c.app->music_eval.has_key())
            return err(code::kBadArg, "no_evaluator: no Gemini key — call configure_music_eval_backend first");
        const std::string intent = b.value("intent", std::string());
        const std::string ref = b.value("reference_path", std::string());
        if (intent.empty() && ref.empty()) return err(code::kBadArg, "need intent and/or reference_path");
        std::vector<uint8_t> wav; json e;
        if (!capture_master_wav(c, b.value("window_seconds", 20.0), wav, e)) return e;
        std::vector<uint8_t> refwav;
        if (!ref.empty()) {
            std::vector<float> rl, rr; uint32_t rsr = 0;
            if (!load_pcm_file(ref, 48000, rl, rr, rsr)) return err(code::kIoError, "could not decode reference_path");
            refwav = pcm16_wav_from_planar(rl, rr, rsr);
        }
        const int id = c.app->music_eval.start_compare(std::move(wav), intent, std::move(refwav));
        if (id < 0) return err(code::kBadArg, "no_evaluator: no Gemini key");
        json r = ok(); r["job_id"] = id; r["status"] = "pending"; return r;
    };

    handlers_["music_eval_result"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app) return err(code::kInternal, "no app context");
        return c.app->music_eval.poll(b.value("job_id", -1));
    };
}

}  // namespace vivid
