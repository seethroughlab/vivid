#pragma once

// Reactive-visuals loop, Phase 2: the multimodal taste lens. The visual analog of MusicEval — sends a
// frame-strip MONTAGE of the live output (assembled from the reactivity ring) plus the numeric audio-
// energy context to Google Gemini, and gets back a structured verdict on whether the visual is
// reactive / legible / on-intent, with concrete issues + fixes. Async (START a job, POLL for the
// result) so control handlers never block the frame loop. Fail-closed: no key → has_key()==false and
// the caller must refuse rather than fake a verdict. There is deliberately NO fake stub backend.
// Shares the Gemini API key with MusicEval (Keychain com.vivid.app / gemini_api_key).

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vivid {

struct VisualEval {
    // Store the Gemini API key (in the Keychain, shared with MusicEval) and/or set the model. Empty
    // api_key leaves the key untouched; empty model keeps the default (gemini-2.5-flash).
    void configure(const std::string& api_key, const std::string& model);

    // {backend: "gemini"|"unconfigured", ready, has_key, model} — ready/backend reflect key presence.
    nlohmann::json status() const;
    bool has_key() const;

    // Start an async judge on a montage PNG. `metrics_context` is the ring's energy sparkline/onset
    // times (injected into the prompt so the model can align frames to audio it can't hear). `intent`
    // is optional free text; `reference_png` is an optional reference montage to compare against.
    // Returns the job id (>0), or -1 if no key is configured — check has_key() and fail closed first.
    int start_judge(std::vector<uint8_t> montage_png, const std::string& intent,
                    const std::string& metrics_context, std::vector<uint8_t> reference_png);

    // {status: "pending"|"done"|"error", ...} — done/error carry the verdict fields (or an error).
    nlohmann::json poll(int job_id) const;

private:
    struct Job {
        std::atomic<int> phase{0};   // 0 pending, 1 done, 2 error (release/acquire fence for `result`)
        nlohmann::json   result;
    };
    std::string model_ = "gemini-2.5-flash";
    mutable std::mutex mtx_;
    std::map<int, std::shared_ptr<Job>> jobs_;
    int next_id_ = 0;
};

}  // namespace vivid
