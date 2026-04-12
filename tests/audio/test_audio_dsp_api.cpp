#include "operator_api/audio_dsp.h"
#include "shared/audio_kernels/audio_buffer_kernels.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "test_helpers.h"

static bool approx(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

static void check_buffer_close(const std::vector<float>& actual,
                               const std::vector<float>& expected,
                               const char* msg,
                               float eps = 1e-5f) {
    float max_diff = 0.0f;
    for (size_t i = 0; i < actual.size(); ++i)
        max_diff = std::max(max_diff, std::fabs(actual[i] - expected[i]));
    check(max_diff <= eps, msg);
}

static void reference_mix4(const float* in1, const float* in2, const float* in3, const float* in4,
                           float* out, uint32_t frames,
                           float g1, float g2, float g3, float g4) {
    for (uint32_t i = 0; i < frames; ++i) {
        const float a = in1 ? in1[i] : 0.0f;
        const float b = in2 ? in2[i] : 0.0f;
        const float c = in3 ? in3[i] : 0.0f;
        const float d = in4 ? in4[i] : 0.0f;
        out[i] = a * g1 + b * g2 + c * g3 + d * g4;
    }
}

static void reference_stereo_pan_width(const float* left_in, const float* right_in,
                                       float* left_out, float* right_out,
                                       uint32_t frames,
                                       float mid_gain, float side_gain,
                                       float width, float pan_left, float pan_right) {
    const float side_scale = side_gain * width;
    for (uint32_t i = 0; i < frames; ++i) {
        const float mid = (left_in[i] + right_in[i]) * 0.5f * mid_gain;
        const float side = (left_in[i] - right_in[i]) * 0.5f * side_scale;
        left_out[i] = (mid + side) * pan_left;
        right_out[i] = (mid - side) * pan_right;
    }
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

    // Shared audio buffer kernels should match the scalar contract.
    {
        std::vector<float> out = {1.0f, -2.0f, 3.0f, -4.0f, 5.0f};
        vivid::audio_kernels::clear(out.data(), static_cast<uint32_t>(out.size()));
        check_buffer_close(out, std::vector<float>(out.size(), 0.0f), "audio_kernels::clear zeros buffer");
    }
    {
        const std::vector<float> in = {1.0f, -0.5f, 0.25f, -0.125f, 0.75f};
        for (float gain : {1.0f, 0.5f, 0.0f}) {
            std::vector<float> out(in.size());
            std::vector<float> expected(in.size());
            for (size_t i = 0; i < in.size(); ++i)
                expected[i] = in[i] * gain;
            vivid::audio_kernels::scale(in.data(), out.data(), static_cast<uint32_t>(in.size()), gain);
            check_buffer_close(out, expected, "audio_kernels::scale matches scalar reference");
        }
    }
    {
        std::vector<float> in_place = {1.0f, -0.5f, 0.25f, -0.125f, 0.75f};
        std::vector<float> expected = in_place;
        for (float& sample : expected)
            sample *= 0.25f;
        vivid::audio_kernels::scale(in_place.data(), in_place.data(),
                                    static_cast<uint32_t>(in_place.size()), 0.25f);
        check_buffer_close(in_place, expected, "audio_kernels::scale supports in-place scaling");
    }
    {
        const std::vector<float> a = {1.0f, 2.0f, 3.0f, 4.0f, -2.0f};
        const std::vector<float> b = {4.0f, 3.0f, 2.0f, 1.0f, -1.0f};
        const std::vector<float> c = {0.5f, 0.5f, 0.5f, 0.5f, 2.0f};
        const std::vector<float> d = {-1.0f, -1.0f, -1.0f, -1.0f, 0.25f};
        std::vector<float> out(a.size());
        std::vector<float> expected(a.size());
        reference_mix4(a.data(), b.data(), c.data(), d.data(), expected.data(),
                       static_cast<uint32_t>(a.size()), 1.0f, 0.5f, 2.0f, -0.25f);
        vivid::audio_kernels::mix4(a.data(), b.data(), c.data(), d.data(), out.data(),
                                   static_cast<uint32_t>(a.size()), 1.0f, 0.5f, 2.0f, -0.25f);
        check_buffer_close(out, expected, "audio_kernels::mix4 all inputs matches scalar reference");
    }
    {
        const std::vector<float> a = {1.0f, 2.0f, 3.0f, 4.0f};
        const std::vector<float> c = {0.5f, 0.5f, 0.5f, 0.5f};
        std::vector<float> out(a.size());
        std::vector<float> expected(a.size());
        reference_mix4(a.data(), nullptr, c.data(), nullptr, expected.data(),
                       static_cast<uint32_t>(a.size()), 0.75f, 9.0f, -2.0f, 4.0f);
        vivid::audio_kernels::mix4(a.data(), nullptr, c.data(), nullptr, out.data(),
                                   static_cast<uint32_t>(a.size()), 0.75f, 9.0f, -2.0f, 4.0f);
        check_buffer_close(out, expected, "audio_kernels::mix4 null inputs match scalar reference");
    }
    {
        const std::vector<float> l = {1.0f, 0.5f, -0.5f, -1.0f, 0.25f};
        const std::vector<float> r = {-1.0f, -0.25f, 0.5f, 1.0f, 0.75f};
        const struct {
            float mid_gain;
            float side_gain;
            float width;
            float pan_l;
            float pan_r;
            const char* label;
        } cases[] = {
            {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, "identity"},
            {1.0f, 1.0f, 0.0f, 1.0f, 1.0f, "mono width"},
            {1.0f, 1.0f, 2.0f, 1.0f, 1.0f, "wide side"},
            {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, "hard left"},
            {1.0f, 1.0f, 1.0f, 0.0f, 1.0f, "hard right"},
        };
        for (const auto& tc : cases) {
            std::vector<float> lo(l.size());
            std::vector<float> ro(r.size());
            std::vector<float> exp_l(l.size());
            std::vector<float> exp_r(r.size());
            reference_stereo_pan_width(l.data(), r.data(), exp_l.data(), exp_r.data(),
                                       static_cast<uint32_t>(l.size()),
                                       tc.mid_gain, tc.side_gain, tc.width, tc.pan_l, tc.pan_r);
            vivid::audio_kernels::stereo_pan_width(l.data(), r.data(), lo.data(), ro.data(),
                                                   static_cast<uint32_t>(l.size()),
                                                   tc.mid_gain, tc.side_gain, tc.width, tc.pan_l, tc.pan_r);
            char msg_l[128];
            char msg_r[128];
            std::snprintf(msg_l, sizeof(msg_l), "audio_kernels::stereo_pan_width %s L", tc.label);
            std::snprintf(msg_r, sizeof(msg_r), "audio_kernels::stereo_pan_width %s R", tc.label);
            check_buffer_close(lo, exp_l, msg_l);
            check_buffer_close(ro, exp_r, msg_r);
        }
        check(vivid::audio_kernels::backend_name()[0] != '\0', "audio kernel backend is named");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
