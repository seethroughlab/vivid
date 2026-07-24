#pragma once
// Audio-file decoding for the sample engine. Ported from vivid-classic's
// decode_wav (operators/shared/sampler_common/sample_bank.h) and adapted to
// decode at the file's NATIVE sample rate and channel count — so the voice
// engine's repitch ratio (sample_rate / device_rate) is correct. Kept in its own
// header so miniaudio is confined to the one translation unit that loads samples
// (the file-load runtime); the op's render path never includes this.
//
// Despite miniaudio's decoder name, ma_decoder_init_file auto-detects the
// container, so this handles WAV, AIFF, MP3, FLAC and OGG/Vorbis — every backend
// compiled into this app's miniaudio. Not wav-only.
#include "audio/sample_engine/sample_data.h"
#include "miniaudio.h"

#include <cstdio>
#include <memory>

namespace vivid {
namespace sample_engine {

// Decode any miniaudio-supported file to f32, split into planar L/R at the file's
// native rate. Returns nullptr on failure (bad path / unreadable / empty).
inline std::shared_ptr<SampleData> decode_audio_native(const std::string& path) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);  // 0/0 = native channels + rate
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) {
        std::fprintf(stderr, "[sample_engine] failed to open: %s\n", path.c_str());
        return nullptr;
    }

    ma_uint64 total_frames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames);
    if (total_frames == 0) {
        std::fprintf(stderr, "[sample_engine] empty or unreadable: %s\n", path.c_str());
        ma_decoder_uninit(&decoder);
        return nullptr;
    }

    const uint32_t channels = decoder.outputChannels;
    const bool stereo = (channels >= 2);

    std::vector<float> interleaved(static_cast<size_t>(total_frames) * channels);
    ma_uint64 frames_read = 0;
    ma_decoder_read_pcm_frames(&decoder, interleaved.data(), total_frames, &frames_read);
    const uint32_t native_rate = decoder.outputSampleRate;
    ma_decoder_uninit(&decoder);

    auto sample = std::make_shared<SampleData>();
    sample->sample_rate = native_rate;
    sample->stereo = stereo;
    sample->samples_L.resize(static_cast<size_t>(frames_read));
    if (stereo) sample->samples_R.resize(static_cast<size_t>(frames_read));
    for (size_t i = 0; i < frames_read; ++i) {
        sample->samples_L[i] = interleaved[i * channels];
        if (stereo) sample->samples_R[i] = interleaved[i * channels + 1];
    }
    return sample;
}

} // namespace sample_engine
} // namespace vivid
