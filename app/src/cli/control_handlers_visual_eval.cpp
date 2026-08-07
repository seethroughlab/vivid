// Reactive-visuals loop, Phase 2: the multimodal visual-judge control surface. Handlers assemble a
// frame-strip MONTAGE of the live output from the reactivity ring (fast, on the UI thread) plus the
// ring's audio-energy context, then hand off to VisualEval, which runs the Gemini call ASYNC and
// returns a job id — so the frame loop never blocks on the network. Fail-closed: with no key
// configured, evaluate/compare return an error rather than a fabricated verdict. No fake stub.
#include "cli/control_handlers.h"
#include "cli/control_handlers_internal.h"
#include "cli/image_analysis_tools.h"   // encode_png, load_image
#include "app/app.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace vivid {

namespace {
double steady_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Build a PNG montage of the last `window` seconds from the reactivity ring. Returns false + fills `e`.
bool build_montage_png(const ControlCtx& c, int cells, double window,
                       std::vector<uint8_t>& png, std::string& ctx, json& e) {
    const double now = steady_seconds();
    std::vector<uint8_t> rgba; uint32_t w = 0, h = 0;
    if (!c.app->reactivity.capture_montage(cells, window, now, rgba, w, h)) {
        e = err(code::kBadArg, "no frames captured yet — is the app playing? (the ring fills while running)");
        return false;
    }
    if (!encode_png(rgba.data(), w, h, png)) { e = err(code::kInternal, "montage PNG encode failed"); return false; }
    ctx = c.app->reactivity.energy_sparkline(window, now);
    return true;
}
}  // namespace

void register_visual_eval_handlers(Handlers& handlers_) {
    // Configure the Gemini backend (shares the key with music-eval). Returns the same status shape.
    handlers_["configure_visual_eval_backend"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app) return err(code::kInternal, "no app context");
        const std::string backend = b.value("backend", std::string("gemini"));
        if (backend != "gemini") return err(code::kBadArg, "only 'gemini' is supported");
        c.app->visual_eval.configure(b.value("api_key", std::string()), b.value("model", std::string()));
        return c.app->visual_eval.status();
    };

    handlers_["visual_eval_status"] = [](const ControlCtx& c, const json&) -> json {
        if (!c.app) return err(code::kInternal, "no app context");
        return c.app->visual_eval.status();
    };

    // Judge whether the live visual is reactive / legible (and on-intent, if given). Async → poll
    // visual_eval_result. `frames` = montage cell count (default 12); `window_seconds` = history window.
    handlers_["evaluate_visual_reactivity"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app) return err(code::kInternal, "no app context");
        if (!c.app->visual_eval.has_key())
            return err(code::kBadArg, "no_evaluator: no Gemini key — call configure_visual_eval_backend first");
        const int cells = b.value("frames", 12);
        const double window = b.value("window_seconds", 4.0);
        std::vector<uint8_t> png; std::string ctx; json e;
        if (!build_montage_png(c, cells, window, png, ctx, e)) return e;
        const int id = c.app->visual_eval.start_judge(std::move(png), b.value("intent", std::string()),
                                                      ctx, {});
        if (id < 0) return err(code::kBadArg, "no_evaluator: no Gemini key");
        json r = ok(); r["job_id"] = id; r["status"] = "pending"; return r;
    };

    // Judge the live visual against a free-text intent and/or a reference image (the intended look).
    handlers_["compare_visual_to_intent"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app) return err(code::kInternal, "no app context");
        if (!c.app->visual_eval.has_key())
            return err(code::kBadArg, "no_evaluator: no Gemini key — call configure_visual_eval_backend first");
        const std::string intent = b.value("intent", std::string());
        const std::string ref = b.value("reference_path", std::string());
        if (intent.empty() && ref.empty()) return err(code::kBadArg, "need intent and/or reference_path");
        const int cells = b.value("frames", 12);
        const double window = b.value("window_seconds", 4.0);
        std::vector<uint8_t> png; std::string ctx; json e;
        if (!build_montage_png(c, cells, window, png, ctx, e)) return e;
        std::vector<uint8_t> ref_png;
        if (!ref.empty()) {
            std::vector<uint8_t> rgba; uint32_t rw = 0, rh = 0;
            if (!load_image(ref, rgba, rw, rh)) return err(code::kIoError, "could not decode reference_path: " + ref);
            if (!encode_png(rgba.data(), rw, rh, ref_png)) return err(code::kInternal, "reference PNG encode failed");
        }
        const int id = c.app->visual_eval.start_judge(std::move(png), intent, ctx, std::move(ref_png));
        if (id < 0) return err(code::kBadArg, "no_evaluator: no Gemini key");
        json r = ok(); r["job_id"] = id; r["status"] = "pending"; return r;
    };

    handlers_["visual_eval_result"] = [](const ControlCtx& c, const json& b) -> json {
        if (!c.app) return err(code::kInternal, "no app context");
        return c.app->visual_eval.poll(b.value("job_id", -1));
    };
}

}  // namespace vivid
