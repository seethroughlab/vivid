#include "operator_api/audio_dsp.h"

#include <cmath>
#include <cstdio>
#include "test_helpers.h"

static bool approx(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

int main() {
    std::fprintf(stderr, "\n=== test_audio_dsp_api ===\n");

    // Trigger detection: only wrap-around should trigger.
    check(audio_dsp::detect_trigger(0.1f, 0.9f), "detect_trigger wrap-around");
    check(!audio_dsp::detect_trigger(0.9f, 0.1f), "detect_trigger forward phase");
    check(!audio_dsp::detect_trigger(0.6f, 0.7f), "detect_trigger small backward phase");

    // Waveform reference points.
    check(approx(audio_dsp::waveform(0.0, 0), 0.0), "sine phase 0");
    check(approx(audio_dsp::waveform(0.25, 0), 1.0), "sine phase 0.25");
    check(approx(audio_dsp::waveform(0.75, 0), -1.0), "sine phase 0.75");
    check(approx(audio_dsp::waveform(0.0, 1), -1.0), "saw phase 0");
    check(approx(audio_dsp::waveform(0.5, 1), 0.0), "saw phase 0.5");
    check(approx(audio_dsp::waveform(0.25, 2), 1.0), "square high");
    check(approx(audio_dsp::waveform(0.75, 2), -1.0), "square low");
    check(approx(audio_dsp::waveform(0.25, 3), 0.0), "triangle phase 0.25");
    check(approx(audio_dsp::waveform(0.5, 3), 1.0), "triangle phase 0.5");
    check(approx(audio_dsp::waveform(0.75, 3), 0.0), "triangle phase 0.75");

    // WhiteNoise should be bounded.
    audio_dsp::WhiteNoise w1;
    for (int i = 0; i < 64; ++i) {
        float a = w1.next();
        check(a >= -1.0f && a <= 1.0f, "WhiteNoise::next in [-1,1]");
        float u = w1.next_unipolar();
        check(u >= 0.0f && u <= 1.0f, "WhiteNoise::next_unipolar in [0,1]");
    }

    // WhiteNoise sequence should be deterministic with identical initial state.
    audio_dsp::WhiteNoise wd1;
    audio_dsp::WhiteNoise wd2;
    for (int i = 0; i < 64; ++i) {
        check(std::fabs(wd1.next() - wd2.next()) < 1e-7f, "WhiteNoise deterministic sequence");
    }

    // PinkNoise should produce bounded, finite values.
    audio_dsp::PinkNoise p;
    for (int i = 0; i < 256; ++i) {
        float v = p.next();
        check(std::isfinite(v), "PinkNoise finite");
        check(v >= -1.0f && v <= 1.0f, "PinkNoise in [-1,1]");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
