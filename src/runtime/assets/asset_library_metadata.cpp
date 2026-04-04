#include "runtime/assets/asset_library_internal.h"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>

namespace vivid::asset_internal {

static constexpr uint32_t kDefaultSamplesPerFrame = 2048;

bool probe_wavetable_metadata(const std::string& path, WavetableAssetMeta& meta) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    ma_result result = ma_decoder_init_file(path.c_str(), &config, &decoder);
    if (result != MA_SUCCESS) {
        std::fprintf(stderr, "[asset_library] Failed to open WAV: %s\n", path.c_str());
        return false;
    }

    ma_uint64 total_frames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames);

    meta.sample_rate = decoder.outputSampleRate;
    meta.channels = decoder.outputChannels;
    meta.samples_per_frame = kDefaultSamplesPerFrame;
    meta.total_samples = static_cast<uint64_t>(total_frames) * meta.channels;

    // For wavetables, frame_count = total mono samples / samples_per_frame.
    // If multi-channel, we use the per-channel frame count.
    uint64_t mono_samples = static_cast<uint64_t>(total_frames);
    meta.frame_count = static_cast<uint32_t>(
        std::min<uint64_t>(mono_samples / kDefaultSamplesPerFrame, 256));

    // Scan for peak amplitude (read all samples)
    float peak = 0.0f;
    constexpr size_t kBufFrames = 4096;
    std::vector<float> buf(kBufFrames * meta.channels);
    for (;;) {
        ma_uint64 read = 0;
        ma_decoder_read_pcm_frames(&decoder, buf.data(), kBufFrames, &read);
        if (read == 0) break;
        for (size_t i = 0; i < static_cast<size_t>(read) * meta.channels; ++i) {
            float a = std::fabs(buf[i]);
            if (a > peak) peak = a;
        }
    }
    meta.peak_amplitude = peak;

    ma_decoder_uninit(&decoder);
    return true;
}

} // namespace vivid::asset_internal
