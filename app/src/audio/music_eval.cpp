#include "audio/music_eval.h"

#include "audio/base64.h"
#include "platform/gemini_client.h"
#include "platform/secret_store.h"

#include <algorithm>
#include <cmath>

namespace vivid {

namespace {
constexpr const char* kService = "com.vivid.app";
constexpr const char* kAccount = "gemini_api_key";

std::string gemini_key() {
    std::string k;
    if (secret_get(kService, kAccount, k) && !k.empty()) return k;   // Keychain (set via the app)
    if (const char* e = std::getenv("GEMINI_API_KEY"); e && *e) return e;   // env fallback (headless/CI)
    if (const char* e = std::getenv("GOOGLE_API_KEY"); e && *e) return e;
    return {};
}

// The analysis prompts, kept close to vivid-classic's proven text (services/music_eval/backends/
// gemini.py). Structure is delivered by a responseSchema (below), so the prompt only needs to steer
// WHAT to analyze, not the output format.
std::string eval_prompt(const std::string& mode) {
    if (mode == "theory")
        return "Provide a detailed music theory analysis of this audio track: key and mode, tempo in "
               "BPM and rhythmic feel, chord progressions and harmonic language, rhythmic structure and "
               "groove, instrumentation and timbral character, and overall form and arrangement. Use "
               "precise music-theory terminology.";
    if (mode == "reasoning")
        return "Think step by step about this audio track: first the rhythm and tempo, then the harmonic "
               "content and key, then the instrumentation and production style, then the overall mood. "
               "State the key and tempo in BPM explicitly.";
    return "Analyze this audio track: the musical key (e.g. 'F minor'), the tempo in BPM, the "
           "instrumentation and production style, the overall mood and aesthetic, and any notable "
           "structural or harmonic features. Be specific and concise.";
}

std::string compare_prompt(const std::string& intent, bool has_reference) {
    std::string p;
    if (has_reference) {
        p = "Compare these two audio clips. The first is the current output; the second is the "
            "reference.";
        if (!intent.empty()) p += " The intended sound is: " + intent + ".";
    } else {
        p = "Evaluate this audio against the following intent: " + intent + ".";
    }
    p += " Score how well the audio matches the intent on each axis from 0.0 (no match) to 1.0 "
         "(perfect match): harmony_match, rhythm_match, timbre_match, structure_match, and an overall "
         "match_score. Be critical — a part described as melodic that is actually a held or repeated "
         "single note should score low. Then list the key deviations from the intent, most important "
         "first. Finish with a one-sentence summary.";
    return p;
}

nlohmann::json eval_schema() {
    return {
        {"type", "OBJECT"},
        {"properties", {
            {"key",             {{"type", "STRING"}}},
            {"tempo_bpm",       {{"type", "NUMBER"}}},
            {"instrumentation", {{"type", "STRING"}}},
            {"mood",            {{"type", "STRING"}}},
            {"summary",         {{"type", "STRING"}}},
        }},
        {"required", nlohmann::json::array({"summary"})},
    };
}

nlohmann::json compare_schema() {
    return {
        {"type", "OBJECT"},
        {"properties", {
            {"match_score",     {{"type", "NUMBER"}}},
            {"harmony_match",   {{"type", "NUMBER"}}},
            {"rhythm_match",    {{"type", "NUMBER"}}},
            {"timbre_match",    {{"type", "NUMBER"}}},
            {"structure_match", {{"type", "NUMBER"}}},
            {"key_deviations",  {{"type", "ARRAY"}, {"items", {{"type", "STRING"}}}}},
            {"summary",         {{"type", "STRING"}}},
        }},
        {"required", nlohmann::json::array({"match_score", "summary"})},
    };
}

// Extract the model's structured JSON from Gemini's generateContent envelope. On any failure sets
// `err` and returns null.
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
        err = "no analysis returned";
        if (env.contains("promptFeedback")) err = "request blocked: " + env["promptFeedback"].dump();
        return {};
    }
}

std::string gemini_url(const std::string& model, const std::string& key) {
    return "https://generativelanguage.googleapis.com/v1beta/models/" + model +
           ":generateContent?key=" + key;
}
}  // namespace

std::vector<uint8_t> pcm16_wav_from_planar(const std::vector<float>& L, const std::vector<float>& R,
                                           uint32_t sr) {
    const uint32_t frames = static_cast<uint32_t>(std::min(L.size(), R.size()));
    const uint16_t channels = 2, bits = 16;
    const uint16_t block_align = channels * (bits / 8);
    const uint32_t byte_rate = sr * block_align;
    const uint32_t data_bytes = frames * block_align;

    std::vector<uint8_t> out;
    out.reserve(44 + data_bytes);
    auto u32 = [&](uint32_t v) { out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF);
                                 out.push_back((v >> 16) & 0xFF); out.push_back((v >> 24) & 0xFF); };
    auto u16 = [&](uint16_t v) { out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF); };
    auto tag = [&](const char* s) { out.insert(out.end(), s, s + 4); };

    tag("RIFF"); u32(36 + data_bytes); tag("WAVE");
    tag("fmt "); u32(16); u16(1 /*PCM*/); u16(channels); u32(sr); u32(byte_rate); u16(block_align); u16(bits);
    tag("data"); u32(data_bytes);

    auto to16 = [](float f) -> uint16_t {
        float x = f * 32767.0f;
        x = std::max(-32768.0f, std::min(32767.0f, x));
        return static_cast<uint16_t>(static_cast<int16_t>(std::lrint(x)));
    };
    for (uint32_t i = 0; i < frames; ++i) { u16(to16(L[i])); u16(to16(R[i])); }
    return out;
}

void MusicEval::configure(const std::string& api_key, const std::string& model) {
    if (!api_key.empty()) secret_set(kService, kAccount, api_key);
    if (!model.empty()) { std::lock_guard<std::mutex> lk(mtx_); model_ = model; }
}

bool MusicEval::has_key() const { return !gemini_key().empty(); }

nlohmann::json MusicEval::status() const {
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

int MusicEval::start_eval(std::vector<uint8_t> wav, const std::string& mode) {
    const std::string key = gemini_key();
    if (key.empty()) return -1;

    auto job = std::make_shared<Job>();
    int id;
    std::string model;
    { std::lock_guard<std::mutex> lk(mtx_); id = ++next_id_; jobs_[id] = job; model = model_; }

    const std::string b64 = plugin_common::base64_encode(wav.data(), wav.size());
    nlohmann::json body = {
        {"contents", nlohmann::json::array({ {
            {"parts", nlohmann::json::array({
                nlohmann::json{{"text", eval_prompt(mode)}},
                nlohmann::json{{"inline_data", {{"mime_type", "audio/wav"}, {"data", b64}}}},
            })}
        } })},
        {"generationConfig", {{"responseMimeType", "application/json"}, {"responseSchema", eval_schema()}}},
    };

    std::weak_ptr<Job> wjob = job;
    std::string mode_copy = mode;
    gemini_post_json(gemini_url(model, key), body.dump(), 90.0,
        [wjob, mode_copy](bool ok, int status, std::string resp) {
            auto j = wjob.lock(); if (!j) return;
            std::string err;
            nlohmann::json inner = parse_gemini(ok, status, resp, err);
            if (!err.empty()) {
                j->result = {{"ok", false}, {"error", {{"code", "inference_error"}, {"message", err}}}};
                j->phase.store(2, std::memory_order_release);
                return;
            }
            j->result = {
                {"ok", true}, {"mode", mode_copy},
                {"key", inner.value("key", std::string())},
                {"tempo_bpm", inner.contains("tempo_bpm") ? inner["tempo_bpm"] : nlohmann::json(nullptr)},
                {"instrumentation", inner.value("instrumentation", std::string())},
                {"mood", inner.value("mood", std::string())},
                {"summary", inner.value("summary", std::string())},
            };
            j->phase.store(1, std::memory_order_release);
        });
    return id;
}

int MusicEval::start_compare(std::vector<uint8_t> wav, const std::string& intent,
                             std::vector<uint8_t> ref_wav) {
    const std::string key = gemini_key();
    if (key.empty()) return -1;

    auto job = std::make_shared<Job>();
    int id;
    std::string model;
    { std::lock_guard<std::mutex> lk(mtx_); id = ++next_id_; jobs_[id] = job; model = model_; }

    const bool has_ref = !ref_wav.empty();
    nlohmann::json parts = nlohmann::json::array();
    parts.push_back(nlohmann::json{{"text", compare_prompt(intent, has_ref)}});
    parts.push_back(nlohmann::json{{"inline_data",
        {{"mime_type", "audio/wav"}, {"data", plugin_common::base64_encode(wav.data(), wav.size())}}}});
    if (has_ref)
        parts.push_back(nlohmann::json{{"inline_data",
            {{"mime_type", "audio/wav"}, {"data", plugin_common::base64_encode(ref_wav.data(), ref_wav.size())}}}});

    nlohmann::json body = {
        {"contents", nlohmann::json::array({ {{"parts", parts}} })},
        {"generationConfig", {{"responseMimeType", "application/json"}, {"responseSchema", compare_schema()}}},
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
            auto num = [&](const char* k) -> nlohmann::json {
                return inner.contains(k) && inner[k].is_number() ? inner[k] : nlohmann::json(nullptr);
            };
            j->result = {
                {"ok", true},
                {"match_score", num("match_score")},
                {"harmony_match", num("harmony_match")}, {"rhythm_match", num("rhythm_match")},
                {"timbre_match", num("timbre_match")},   {"structure_match", num("structure_match")},
                {"key_deviations", inner.contains("key_deviations") && inner["key_deviations"].is_array()
                                       ? inner["key_deviations"] : nlohmann::json::array()},
                {"summary", inner.value("summary", std::string())},
                {"had_reference", has_ref}, {"had_intent", had_intent},
            };
            j->phase.store(1, std::memory_order_release);
        });
    return id;
}

nlohmann::json MusicEval::poll(int job_id) const {
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
