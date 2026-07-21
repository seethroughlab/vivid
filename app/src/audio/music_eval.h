#pragma once

// ADR-0026: semantic music evaluation, in-app, Gemini-backed. Captures the live master output,
// sends it to Google Gemini with the intent, and grades it (key/tempo/instrumentation/mood, and an
// intent match_score with harmony/rhythm/timbre/structure sub-scores). The Gemini call is async (it
// takes seconds), so callers START a job and POLL for the result — control handlers never block the
// frame loop. Fail-closed: with no key configured this is a no-op (has_key() == false), and the
// caller must refuse rather than fake a pass. There is deliberately NO fake stub backend.

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vivid {

// Build a 16-bit PCM WAV (RIFF) container from a planar stereo capture snapshot (what
// Transport::capture_snapshot returns). This is the audio/wav payload sent inline to Gemini.
std::vector<uint8_t> pcm16_wav_from_planar(const std::vector<float>& L, const std::vector<float>& R,
                                           uint32_t sr);

struct MusicEval {
    // Store the Gemini API key (in the Keychain) and/or set the model. Empty api_key leaves the key
    // untouched; empty model keeps the default (gemini-2.5-flash).
    void configure(const std::string& api_key, const std::string& model);

    // {backend: "gemini"|"unconfigured", ready, has_key, model}. `backend`/`ready` are "gemini"/true
    // only when a key is present — the fail-closed signal callers check before trusting a verdict.
    nlohmann::json status() const;
    bool has_key() const;

    // Start an async Gemini job on the given WAV; returns a job id (>0). `mode` is
    // caption|theory|reasoning. compare scores against a free-text intent (and an optional reference
    // WAV). Returns -1 if no key is configured — callers should check has_key() and fail closed first.
    int start_eval(std::vector<uint8_t> wav, const std::string& mode);
    int start_compare(std::vector<uint8_t> wav, const std::string& intent, std::vector<uint8_t> ref_wav);

    // {status: "pending"|"done"|"error", ...} — done/error carry the result fields (or an error).
    nlohmann::json poll(int job_id) const;

private:
    struct Job {
        std::atomic<int> phase{0};   // 0 pending, 1 done, 2 error (release/acquire fence for `result`)
        nlohmann::json   result;     // written once by the completion thread before `phase` flips
    };
    std::string model_ = "gemini-2.5-flash";
    mutable std::mutex mtx_;
    std::map<int, std::shared_ptr<Job>> jobs_;
    int next_id_ = 0;
};

}  // namespace vivid
