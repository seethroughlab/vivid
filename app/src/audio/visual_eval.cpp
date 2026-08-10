#include "audio/visual_eval.h"

#include "audio/base64.h"
#include "platform/gemini_client.h"
#include "platform/secret_store.h"

namespace vivid {

namespace {
// Shared with MusicEval — the same Keychain key backs both audio and visual evaluation.
constexpr const char* kService = "com.vivid.app";
constexpr const char* kAccount = "gemini_api_key";

std::string gemini_key() {
    std::string k;
    if (secret_get(kService, kAccount, k) && !k.empty()) return k;   // Keychain (set via the app)
    if (const char* e = std::getenv("GEMINI_API_KEY"); e && *e) return e;   // env fallback (headless/CI)
    if (const char* e = std::getenv("GOOGLE_API_KEY"); e && *e) return e;
    return {};
}

std::string gemini_url(const std::string& model, const std::string& key) {
    return "https://generativelanguage.googleapis.com/v1beta/models/" + model +
           ":generateContent?key=" + key;
}

std::string judge_prompt(const std::string& intent, const std::string& metrics_context, bool has_reference) {
    std::string p =
        "The tiled frames are sampled left-to-right, then top-to-bottom, over the last few seconds of a "
        "MUSIC-REACTIVE visual (a procedural 3D scene driven by live audio). ";
    p += metrics_context.empty() ? "" : ("Audio over the same window: " + metrics_context + ". ");
    if (has_reference)
        p += "A second montage is a REFERENCE for the intended look. ";
    p += "Judge the visual on three axes and be CRITICAL (default to the lower score when unsure):\n"
         "1. reactive — do the visible changes across frames look CAUSED by the audio events/energy "
         "above (a hit produces a visible burst/scale-pop; energy drives an obvious change), rather than "
         "arbitrary animation?\n"
         "2. legible — can a viewer clearly SEE the music driving the form? Reactivity reads as caused "
         "by sound only when it is PUNCTUAL (a note-on triggers a visible spawn/burst) or MONOTONIC-AND-"
         "LARGE (bass drives an obvious scale/inflation). Subtle multi-parameter wiggle should score LOW.\n";
    if (!intent.empty())
        p += "3. on_intent — how well does it match this intent: \"" + intent + "\"?\n";
    else
        p += "3. on_intent — leave true with a neutral score; no intent was given.\n";
    p += "Score each axis 0.0 (no) to 1.0 (yes). Then list concrete ISSUES and specific, actionable "
         "FIXES in this tool's vocabulary (e.g. 'drive scale_y as well as scale_x', 'raise the mapping "
         "low end so the form doesn't vanish between hits', 'add a note-on spawn for a punctual layer', "
         "'shorten the release so hits land on the beat'). Finish with a one-sentence summary.";
    return p;
}

nlohmann::json judge_schema() {
    return {
        {"type", "OBJECT"},
        {"properties", {
            {"reactive",          {{"type", "BOOLEAN"}}},
            {"legible",           {{"type", "BOOLEAN"}}},
            {"on_intent",         {{"type", "BOOLEAN"}}},
            {"reactive_score",    {{"type", "NUMBER"}}},
            {"legibility_score",  {{"type", "NUMBER"}}},
            {"intent_score",      {{"type", "NUMBER"}}},
            {"issues",            {{"type", "ARRAY"}, {"items", {{"type", "STRING"}}}}},
            {"fixes",             {{"type", "ARRAY"}, {"items", {{"type", "STRING"}}}}},
            {"summary",           {{"type", "STRING"}}},
        }},
        {"required", nlohmann::json::array({"summary"})},
    };
}

// Extract the model's structured JSON from Gemini's generateContent envelope. On any failure sets
// `err` and returns null. (Mirrors MusicEval's parser — kept local to preserve the parallel structure.)
nlohmann::json parse_gemini(bool ok, int status, const std::string& resp, std::string& err) {
    if (!ok) { err = resp.empty() ? "network error" : resp; return {}; }
    nlohmann::json env;
    try { env = nlohmann::json::parse(resp); }
    catch (...) { err = "invalid response from Gemini (not JSON)"; return {}; }
    if (status != 200) {
        err = "HTTP " + std::to_string(status);
        if (env.contains("error") && env["error"].contains("message"))
            err = env["error"]["message"].get<std::string>();
        return {};
    }
    try {
        const std::string text = env.at("candidates").at(0).at("content").at("parts").at(0)
                                    .at("text").get<std::string>();
        return nlohmann::json::parse(text);   // responseMimeType=application/json → `text` IS the JSON
    } catch (...) {
        err = "no verdict returned";
        if (env.contains("promptFeedback")) err = "request blocked: " + env["promptFeedback"].dump();
        return {};
    }
}
}  // namespace

void VisualEval::configure(const std::string& api_key, const std::string& model) {
    if (!api_key.empty()) secret_set(kService, kAccount, api_key);
    if (!model.empty()) { std::lock_guard<std::mutex> lk(mtx_); model_ = model; }
}

bool VisualEval::has_key() const { return !gemini_key().empty(); }

nlohmann::json VisualEval::status() const {
    const bool key = has_key();
    std::string model;
    { std::lock_guard<std::mutex> lk(mtx_); model = model_; }
    return {
        {"ok", true},
        {"backend", key ? "gemini" : "unconfigured"},
        {"ready", key},
        {"has_key", key},
        {"model", model},
    };
}

int VisualEval::start_judge(std::vector<uint8_t> montage_png, const std::string& intent,
                            const std::string& metrics_context, std::vector<uint8_t> reference_png) {
    const std::string key = gemini_key();
    if (key.empty()) return -1;

    auto job = std::make_shared<Job>();
    int id;
    std::string model;
    { std::lock_guard<std::mutex> lk(mtx_); id = ++next_id_; jobs_[id] = job; model = model_; }

    const bool has_ref = !reference_png.empty();
    nlohmann::json parts = nlohmann::json::array();
    parts.push_back(nlohmann::json{{"text", judge_prompt(intent, metrics_context, has_ref)}});
    parts.push_back(nlohmann::json{{"inline_data",
        {{"mime_type", "image/png"}, {"data", plugin_common::base64_encode(montage_png.data(), montage_png.size())}}}});
    if (has_ref)
        parts.push_back(nlohmann::json{{"inline_data",
            {{"mime_type", "image/png"}, {"data", plugin_common::base64_encode(reference_png.data(), reference_png.size())}}}});

    nlohmann::json body = {
        {"contents", nlohmann::json::array({ {{"parts", parts}} })},
        {"generationConfig", {{"responseMimeType", "application/json"}, {"responseSchema", judge_schema()}}},
    };

    std::weak_ptr<Job> wjob = job;
    const bool had_intent = !intent.empty();
    gemini_post_json(gemini_url(model, key), body.dump(), 90.0,
        [wjob, has_ref, had_intent](bool ok, int status, std::string resp) {
            auto j = wjob.lock(); if (!j) return;
            std::string err;
            nlohmann::json inner = parse_gemini(ok, status, resp, err);
            if (!err.empty()) {
                j->result = {{"ok", false}, {"error", {{"code", "inference_error"}, {"message", err}}}};
                j->phase.store(2, std::memory_order_release);
                return;
            }
            auto bl = [&](const char* k) -> nlohmann::json {
                return inner.contains(k) && inner[k].is_boolean() ? inner[k] : nlohmann::json(nullptr);
            };
            auto num = [&](const char* k) -> nlohmann::json {
                return inner.contains(k) && inner[k].is_number() ? inner[k] : nlohmann::json(nullptr);
            };
            auto arr = [&](const char* k) -> nlohmann::json {
                return inner.contains(k) && inner[k].is_array() ? inner[k] : nlohmann::json::array();
            };
            j->result = {
                {"ok", true},
                {"reactive", bl("reactive")}, {"legible", bl("legible")}, {"on_intent", bl("on_intent")},
                {"reactive_score", num("reactive_score")}, {"legibility_score", num("legibility_score")},
                {"intent_score", num("intent_score")},
                {"issues", arr("issues")}, {"fixes", arr("fixes")},
                {"summary", inner.value("summary", std::string())},
                {"had_reference", has_ref}, {"had_intent", had_intent},
            };
            j->phase.store(1, std::memory_order_release);
        });
    return id;
}

nlohmann::json VisualEval::poll(int job_id) const {
    std::shared_ptr<Job> job;
    { std::lock_guard<std::mutex> lk(mtx_);
      auto it = jobs_.find(job_id);
      if (it == jobs_.end()) return {{"ok", false}, {"status", "error"},
                                     {"error", {{"code", "not_found"}, {"message", "unknown job id"}}}};
      job = it->second; }
    const int phase = job->phase.load(std::memory_order_acquire);
    if (phase == 0) return {{"ok", true}, {"status", "pending"}};
    nlohmann::json r = job->result;                    // safe: written before the release store
    r["status"] = (phase == 1) ? "done" : "error";
    return r;
}

}  // namespace vivid
