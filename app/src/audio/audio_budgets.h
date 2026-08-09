#pragma once
// ADR-0031 — realtime audio budgets, read ONCE from the environment and cached.
//
// getenv is not RT-safe, so audio_budgets() reads every tunable exactly once into a function-local
// static and hands back a reference forever after. Warm audio_budgets() on the MAIN thread during
// audio setup — before the RT audio thread starts — exactly like watchdog_config() (plugin_watchdog.h),
// so the RT thread never triggers the first-call getenv init.
//
// This is the single home for the callback-budget assumptions ADR-0031 §6 wants owned in code rather
// than scattered as literals: max supported block size, device period, expected sample rate, the
// over-budget multiplier, the bail-to-silence Error threshold, and the concurrency-harness knobs
// (stress duration + allowed try_lock skips). Both the RT health counters (audio_health.h) and the
// tests read the SAME values from here.
//
// The struct only *defaults to* the values that are the compile-time authority elsewhere
// (kGraphMaxBlock for pool sizing, periodSizeInFrames for device init); it does not drive them. The
// kGraphMaxBlock coupling is guarded by a static_assert at that header (vst3_host_internal.h) against
// kDefaultMaxBlockFrames below.
#include <cstdint>
#include <cstdlib>

namespace vivid::audio {

// Defaults, owned here so downstream headers can static_assert against them without a runtime read.
inline constexpr uint32_t kDefaultMaxBlockFrames = 4096u;  // == kGraphMaxBlock (pool stride authority)
inline constexpr uint32_t kDefaultDevicePeriod   = 1024u;  // == periodSizeInFrames (device init)
inline constexpr uint32_t kDefaultSampleRate     = 48000u;
inline constexpr double   kDefaultBudgetMult     = 1.0;    // one block alone exceeding realtime is over budget
inline constexpr uint32_t kDefaultBailoutErr     = 4u;     // consecutive real bailouts that roll up to Error

struct AudioBudgets {
    uint32_t max_block_frames;      // == kGraphMaxBlock (4096); larger blocks render silence
    uint32_t device_period_frames; // == periodSizeInFrames (1024); nominal callback block
    uint32_t expected_sample_rate; // 48000 nominal; 0 = accept any
    double   callback_budget_mult; // over-budget threshold = mult * the block's realtime duration
    uint32_t bailout_error_count;  // consecutive render bail-to-silence blocks that roll up to Error
    uint32_t stress_ms;            // concurrency-harness run duration (0 = keep the fixed-iteration loop)
    uint32_t allowed_skips;        // harness: max tolerated try_lock skips (0 = assertion disabled)
};

// Pure env parse — reads the environment fresh each call (NOT cached), so tests can setenv and observe
// the result. audio_budgets() is the cached, RT-facing accessor built on top of this.
inline AudioBudgets parse_audio_budgets() {
    auto envu = [](const char* k, uint32_t d) {
        const char* v = std::getenv(k);
        return v ? static_cast<uint32_t>(std::strtoul(v, nullptr, 10)) : d;
    };
    auto envd = [](const char* k, double d) {
        const char* v = std::getenv(k);
        return v ? std::atof(v) : d;
    };
    AudioBudgets c{};
    c.max_block_frames     = envu("VIVID_AUDIO_MAX_BLOCK",   kDefaultMaxBlockFrames);
    c.device_period_frames = envu("VIVID_AUDIO_PERIOD",      kDefaultDevicePeriod);
    c.expected_sample_rate = envu("VIVID_AUDIO_SR",          kDefaultSampleRate);
    c.callback_budget_mult = envd("VIVID_AUDIO_BUDGET_MULT", kDefaultBudgetMult);
    c.bailout_error_count  = envu("VIVID_AUDIO_BAILOUT_ERR", kDefaultBailoutErr);
    c.stress_ms            = envu("VIVID_AUDIO_STRESS_MS",   0u);
    c.allowed_skips        = envu("VIVID_AUDIO_ALLOWED_SKIPS", 0u);
    // Clamp nonsensical overrides back to safe defaults (mirrors watchdog_config()).
    if (!(c.callback_budget_mult > 0.0)) c.callback_budget_mult = kDefaultBudgetMult;
    if (c.max_block_frames == 0)     c.max_block_frames = kDefaultMaxBlockFrames;
    if (c.device_period_frames == 0) c.device_period_frames = kDefaultDevicePeriod;
    if (c.bailout_error_count == 0)  c.bailout_error_count = kDefaultBailoutErr;
    return c;
}

inline const AudioBudgets& audio_budgets() {
    static const AudioBudgets b = parse_audio_budgets();
    return b;
}

}  // namespace vivid::audio
