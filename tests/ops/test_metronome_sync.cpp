#include "operator_api/metronome_sync.h"
#include "operator_api/gpu_operator.h"
#include "test_helpers.h"

#include <cmath>
#include <cstdio>

static void test_transport_parity() {
    std::fprintf(stderr, "\n--- metronome transport parity across frame/audio/gpu ---\n");

    VividFrameContext frame{};
    VividAudioContext audio{};
    VividGpuContext gpu{};

    frame.metronome_bpm = 93.0f;
    frame.metronome_beats_per_bar = 5;
    frame.metronome_beats_elapsed = 7.25;
    frame.metronome_beat_phase = 0.25f;
    frame.metronome_bar_phase = 0.45f;
    frame.metronome_beat_ms = 645.1613f;

    audio.metronome_bpm = frame.metronome_bpm;
    audio.metronome_beats_per_bar = frame.metronome_beats_per_bar;
    audio.metronome_beats_elapsed = frame.metronome_beats_elapsed;
    audio.metronome_beat_phase = frame.metronome_beat_phase;
    audio.metronome_bar_phase = frame.metronome_bar_phase;
    audio.metronome_beat_ms = frame.metronome_beat_ms;

    gpu.metronome_bpm = frame.metronome_bpm;
    gpu.metronome_beats_per_bar = frame.metronome_beats_per_bar;
    gpu.metronome_beats_elapsed = frame.metronome_beats_elapsed;
    gpu.metronome_beat_phase = frame.metronome_beat_phase;
    gpu.metronome_bar_phase = frame.metronome_bar_phase;
    gpu.metronome_beat_ms = frame.metronome_beat_ms;

    const auto fm = vivid::metronome_transport(&frame);
    const auto am = vivid::metronome_transport(&audio);
    const auto gm = vivid::metronome_transport(&gpu);

    check_float(fm.bpm, am.bpm, 1e-6f, "frame/audio bpm parity");
    check_float(fm.bpm, gm.bpm, 1e-6f, "frame/gpu bpm parity");
    check(fm.beats_per_bar == am.beats_per_bar && am.beats_per_bar == gm.beats_per_bar,
          "beats_per_bar parity");
    check_float(static_cast<float>(fm.beats_elapsed), static_cast<float>(am.beats_elapsed), 1e-6f,
                "frame/audio beats_elapsed parity");
    check_float(static_cast<float>(fm.beats_elapsed), static_cast<float>(gm.beats_elapsed), 1e-6f,
                "frame/gpu beats_elapsed parity");
    check_float(fm.beat_phase, gm.beat_phase, 1e-6f, "beat_phase parity");
    check_float(fm.bar_phase, gm.bar_phase, 1e-6f, "bar_phase parity");
    check_float(fm.beat_ms, gm.beat_ms, 1e-4f, "beat_ms parity");
}

int main() {
    std::fprintf(stderr, "\n=== Test: Metronome Sync Helpers ===\n");
    test_transport_parity();
    std::fprintf(stderr, "\n%s (%d failure%s)\n",
                 failures ? "FAILED" : "PASSED",
                 failures,
                 failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
