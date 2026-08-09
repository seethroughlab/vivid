// Headless test for the ADR-0031 §6 audio-budgets config. Pure header (no App/GPU/audio libs), so it
// runs on the portable leg. Exercises the defaults, the env-override parse, and the clamp-to-safe
// behavior. The kGraphMaxBlock coupling itself is a compile-time static_assert in vst3_host_internal.h
// (too heavy to include here); this test pins the default constant that assert compares against.
#include "audio/audio_budgets.h"
#include "test_helpers.h"

#include <cstdlib>
#include <initializer_list>

using vivid::audio::AudioBudgets;
using vivid::audio::parse_audio_budgets;
using vivid::audio::kDefaultMaxBlockFrames;

int main() {
    // The default constant the vst3_host_internal.h static_assert pins to kGraphMaxBlock.
    CHECK(kDefaultMaxBlockFrames == 4096u);

    // Defaults, with no environment set (clear anything a parent shell might carry).
    for (const char* k : {"VIVID_AUDIO_MAX_BLOCK", "VIVID_AUDIO_PERIOD", "VIVID_AUDIO_SR",
                          "VIVID_AUDIO_BUDGET_MULT", "VIVID_AUDIO_BAILOUT_ERR",
                          "VIVID_AUDIO_STRESS_MS", "VIVID_AUDIO_ALLOWED_SKIPS"})
        unsetenv(k);
    AudioBudgets d = parse_audio_budgets();
    CHECK(d.max_block_frames == 4096u);
    CHECK(d.device_period_frames == 1024u);
    CHECK(d.expected_sample_rate == 48000u);
    CHECK(d.callback_budget_mult == 1.0);
    CHECK(d.bailout_error_count == 4u);
    CHECK(d.stress_ms == 0u);
    CHECK(d.allowed_skips == 0u);

    // Env overrides are parsed.
    setenv("VIVID_AUDIO_MAX_BLOCK", "2048", 1);
    setenv("VIVID_AUDIO_PERIOD", "512", 1);
    setenv("VIVID_AUDIO_SR", "44100", 1);
    setenv("VIVID_AUDIO_BUDGET_MULT", "1.5", 1);
    setenv("VIVID_AUDIO_BAILOUT_ERR", "8", 1);
    setenv("VIVID_AUDIO_STRESS_MS", "2000", 1);
    setenv("VIVID_AUDIO_ALLOWED_SKIPS", "3", 1);
    AudioBudgets o = parse_audio_budgets();
    CHECK(o.max_block_frames == 2048u);
    CHECK(o.device_period_frames == 512u);
    CHECK(o.expected_sample_rate == 44100u);
    CHECK(o.callback_budget_mult == 1.5);
    CHECK(o.bailout_error_count == 8u);
    CHECK(o.stress_ms == 2000u);
    CHECK(o.allowed_skips == 3u);

    // Nonsensical values clamp back to safe defaults (never 0 / non-positive on the RT-facing fields).
    setenv("VIVID_AUDIO_MAX_BLOCK", "0", 1);
    setenv("VIVID_AUDIO_PERIOD", "0", 1);
    setenv("VIVID_AUDIO_BUDGET_MULT", "0", 1);
    setenv("VIVID_AUDIO_BAILOUT_ERR", "0", 1);
    AudioBudgets c = parse_audio_budgets();
    CHECK(c.max_block_frames == 4096u);
    CHECK(c.device_period_frames == 1024u);
    CHECK(c.callback_budget_mult == 1.0);
    CHECK(c.bailout_error_count == 4u);

    return vivid::test::summary("test_audio_budgets");
}
