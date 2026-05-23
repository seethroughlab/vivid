#include "shared/sampler_common/voice.h"

#include <cmath>
#include <cstdio>
#include <memory>

#include "test_helpers.h"

namespace {

constexpr float kDt = 1.0f / 48000.0f;

std::shared_ptr<vivid_sampler::SampleData> make_sample(
    std::initializer_list<float> samples) {
    auto data = std::make_shared<vivid_sampler::SampleData>();
    data->samples_L.assign(samples.begin(), samples.end());
    data->samples_R = data->samples_L;
    data->sample_rate = 48000;
    data->stereo = true;
    return data;
}

std::shared_ptr<vivid_sampler::SampleData> make_ramp(uint32_t count) {
    auto data = std::make_shared<vivid_sampler::SampleData>();
    data->samples_L.resize(count);
    data->samples_R.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        data->samples_L[i] = static_cast<float>(i);
        data->samples_R[i] = static_cast<float>(i);
    }
    data->sample_rate = 48000;
    data->stereo = true;
    return data;
}

vivid_sampler::SampleRegion make_region(
    std::shared_ptr<vivid_sampler::SampleData> data,
    bool loop = false,
    uint32_t loop_start = 0,
    uint32_t loop_end = 0) {
    vivid_sampler::SampleRegion region;
    region.data = std::move(data);
    region.loop_enabled = loop;
    region.loop_start = loop_start;
    region.loop_end = loop_end;
    return region;
}

float render_one(vivid_sampler::Voice& voice,
                 vivid_sampler::VoiceRenderOptions options = {}) {
    float l = 0.0f;
    float r = 0.0f;
    vivid_sampler::voice_render_frame(
        voice, l, r, kDt,
        0.001f, 0.001f, 1.0f, 0.001f,
        1.0f, 1.0f, options);
    return l;
}

void warm_envelope(vivid_sampler::Voice& voice,
                   vivid_sampler::VoiceRenderOptions options = {}) {
    for (int i = 0; i < 64; ++i) {
        render_one(voice, options);
    }
}

void set_full_envelope(vivid_sampler::Voice& voice) {
    voice.envelope.stage = vivid::adsr::SUSTAIN;
    voice.envelope.env_value = 1.0f;
    voice.envelope.env_progress = 0.0f;
}

void test_detune_rate_equivalent_advances_faster() {
    std::fprintf(stderr, "\n--- detune-rate equivalent advances faster ---\n");

    auto data = make_ramp(256);
    auto region = make_region(data, true, 0, 256);
    vivid_sampler::Voice normal;
    vivid_sampler::Voice octave;

    vivid_sampler::voice_note_on(normal, 60, 1.0f, &region, 1.0, 1, false);
    vivid_sampler::voice_note_on(octave, 60, 1.0f, &region, std::pow(2.0, 12.0 / 12.0), 1, false);

    warm_envelope(normal);
    warm_envelope(octave);
    check_float(static_cast<float>(normal.playback_pos), 64.0f, 0.01f,
                "normal playback advances one frame per sample");
    check_float(static_cast<float>(octave.playback_pos), 128.0f, 0.01f,
                "12 semitone detune-rate equivalent advances two frames per sample");
}

void test_reverse_one_shot_reads_backwards_and_stops() {
    std::fprintf(stderr, "\n--- reverse one-shot reads backwards and stops ---\n");

    auto data = make_sample({0.0f, 10.0f, 20.0f, 30.0f});
    auto region = make_region(data);
    vivid_sampler::Voice voice;
    vivid_sampler::voice_note_on(voice, 60, 1.0f, &region, 1.0, 1, true, true);

    vivid_sampler::VoiceRenderOptions options;
    options.reverse = true;

    set_full_envelope(voice);
    check_float(render_one(voice, options), 30.0f, 1e-4f,
                "reverse one-shot starts at sample end");
    warm_envelope(voice, options);
    check(voice.playback_pos < 0.0, "reverse one-shot moves before sample start");
    check(!voice.active, "reverse one-shot deactivates after crossing sample start");
}

void test_reverse_loop_wraps_to_loop_end() {
    std::fprintf(stderr, "\n--- reverse loop wraps to loop end ---\n");

    auto data = make_sample({0.0f, 1.0f, 2.0f, 3.0f});
    auto region = make_region(data, true, 1, 4);
    vivid_sampler::Voice voice;
    vivid_sampler::voice_note_on(voice, 60, 1.0f, &region, 1.0, 1, false, true);

    vivid_sampler::VoiceRenderOptions options;
    options.reverse = true;
    warm_envelope(voice, options);

    check(voice.active, "reverse loop stays active");
    check(voice.playback_pos >= 1.0 && voice.playback_pos < 4.0,
          "reverse loop playback position remains inside loop region");
}

void test_crossfade_zero_preserves_hard_boundary() {
    std::fprintf(stderr, "\n--- loop crossfade zero preserves hard boundary ---\n");

    auto data = make_sample({1.0f, 1.0f, 1.0f, -1.0f});
    auto region = make_region(data, true, 0, 4);
    vivid_sampler::Voice voice;
    vivid_sampler::voice_note_on(voice, 60, 1.0f, &region, 1.0, 1, false);
    voice.playback_pos = 3.0;
    set_full_envelope(voice);

    vivid_sampler::VoiceRenderOptions options;
    options.loop_crossfade_frames = 0;
    const float out = render_one(voice, options);
    check(out < -0.5f, "zero crossfade renders unblended loop tail");
}

void test_crossfade_blends_tail_and_head() {
    std::fprintf(stderr, "\n--- loop crossfade blends tail and head ---\n");

    auto data = make_sample({1.0f, 1.0f, 1.0f, -1.0f});
    auto region = make_region(data, true, 0, 4);
    vivid_sampler::Voice voice;
    vivid_sampler::voice_note_on(voice, 60, 1.0f, &region, 1.0, 1, false);
    voice.playback_pos = 3.0;
    set_full_envelope(voice);

    vivid_sampler::VoiceRenderOptions options;
    options.loop_crossfade_frames = 2;
    const float out = render_one(voice, options);
    check(out > -0.5f, "crossfade blends loop tail toward loop head");
}

}  // namespace

int main() {
    test_detune_rate_equivalent_advances_faster();
    test_reverse_one_shot_reads_backwards_and_stops();
    test_reverse_loop_wraps_to_loop_end();
    test_crossfade_zero_preserves_hard_boundary();
    test_crossfade_blends_tail_and_head();
    return failures == 0 ? 0 : 1;
}
