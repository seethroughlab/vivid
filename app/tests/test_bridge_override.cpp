// ADR-0030 Phase 2: the frame-side bridge's NON-DESTRUCTIVE override channel on native audio ops.
// Proves (a) the accessor contract — an override changes the EFFECTIVE base the render uses while the
// authored base (audio_op_param_get / pvals) is never touched, and clearing returns to the authored
// base — and (b) that the override actually reaches the DSP: TestTone's output amplitude scales with
// its gain param, so driving the override rescales the sound, and clearing restores it. Portable
// (no plugin), so it runs in the Linux tier too.
#include "audio/audio_op_runtime.h"
#include "audio/builtin_audio_ops.h"
#include "gpu/op_runtime.h"
#include "midi/midi_clip.h"    // vivid::session::NoteEvent
#include "test_helpers.h"

#include <cmath>
#include <vector>

using namespace vivid;
using namespace vivid::test;

// Peak |sample| of a held note rendered through a fresh op at gain `base` with optional override.
// A long block spans many sine cycles, so the peak reaches the amplitude regardless of start phase.
static float peak_at(OpRegistry& reg, float base, bool override_on, float override_v) {
    AudioOp* op = audio_op_create(reg, "TestTone");
    audio_op_param_set(op, 0, base);                 // authored base gain
    if (override_on) audio_op_param_override_set(op, 0, override_v);
    const uint32_t frames = 4096, sr = 48000;
    std::vector<float> L(frames, 0.f), R(frames, 0.f);
    session::NoteEvent on{ 0, true, 69, 1.0f, 1, 0 };   // A4, full velocity, at sample 0
    audio_op_process(op, L.data(), R.data(), frames, sr, 120.f, 4, 0.0, &on, 1);
    float pk = 0.f;
    for (float x : L) pk = std::max(pk, std::fabs(x));
    audio_op_destroy(op);
    return pk;
}

int main() {
    OpRegistry reg;
    register_builtin_audio_ops(reg);

    // --- Accessor contract (exact) -------------------------------------------------------------
    AudioOp* op = audio_op_create(reg, "TestTone");
    CHECK(op != nullptr);
    audio_op_param_set(op, 0, 0.5f);                       // authored base
    CHECK_NEAR(audio_op_param_get(op, 0), 0.5f, 1e-6);
    CHECK_NEAR(audio_op_param_effective(op, 0), 0.5f, 1e-6);   // no override yet → effective == base

    audio_op_param_override_set(op, 0, 0.2f);              // bridge drives it
    CHECK_NEAR(audio_op_param_get(op, 0), 0.5f, 1e-6);         // authored base UNTOUCHED
    CHECK_NEAR(audio_op_param_effective(op, 0), 0.2f, 1e-6);   // effective follows the override

    audio_op_param_override_set(op, 0, 0.9f);              // a moving mapping just updates the value
    CHECK_NEAR(audio_op_param_get(op, 0), 0.5f, 1e-6);
    CHECK_NEAR(audio_op_param_effective(op, 0), 0.9f, 1e-6);

    audio_op_param_override_clear(op, 0);                  // mapping disconnected
    CHECK_NEAR(audio_op_param_get(op, 0), 0.5f, 1e-6);         // still the authored base
    CHECK_NEAR(audio_op_param_effective(op, 0), 0.5f, 1e-6);   // effective returns to base
    audio_op_destroy(op);

    // --- The override actually reaches the DSP (behavioral) ------------------------------------
    // TestTone output amplitude ≈ vel*0.25*gain for a single voice; here vel=1 → peak ≈ 0.25*gain.
    const float pk_base = peak_at(reg, 0.4f, /*override*/ false, 0.f);       // base 0.4
    const float pk_ovr  = peak_at(reg, 0.4f, /*override*/ true,  0.8f);      // override 0.8 (2x base)
    const float pk_clr  = peak_at(reg, 0.4f, /*override*/ false, 0.f);       // back to base

    CHECK_NEAR(pk_base, 0.25f * 0.4f, 0.02f);        // ≈ 0.10
    CHECK_NEAR(pk_ovr,  0.25f * 0.8f, 0.02f);        // ≈ 0.20 — the override doubled the level
    CHECK(pk_ovr > pk_base * 1.5f);                  // unmistakably louder under the override
    CHECK_NEAR(pk_clr, pk_base, 1e-4);               // clearing restores the base-driven level

    return summary("bridge_override");
}
