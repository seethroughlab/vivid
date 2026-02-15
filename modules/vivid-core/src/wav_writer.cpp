// Vivid - WAV file read/write using miniaudio

#include <vivid/wav_writer.h>
#include "miniaudio.h"
#include <iostream>

namespace vivid {

bool writeWAV(const std::string& path, const float* samples,
              uint32_t frameCount, uint32_t channels, uint32_t sampleRate) {
    if (!samples || frameCount == 0 || channels == 0 || sampleRate == 0) {
        std::cerr << "[WAV] Invalid parameters for writeWAV" << std::endl;
        return false;
    }

    ma_encoder_config config = ma_encoder_config_init(
        ma_encoding_format_wav,
        ma_format_f32,
        channels,
        sampleRate
    );

    ma_encoder encoder;
    ma_result result = ma_encoder_init_file(path.c_str(), &config, &encoder);
    if (result != MA_SUCCESS) {
        std::cerr << "[WAV] Failed to open file for writing: " << path << std::endl;
        return false;
    }

    ma_uint64 framesWritten;
    result = ma_encoder_write_pcm_frames(&encoder, samples, frameCount, &framesWritten);
    ma_encoder_uninit(&encoder);

    if (result != MA_SUCCESS || framesWritten != frameCount) {
        std::cerr << "[WAV] Failed to write all frames to: " << path << std::endl;
        return false;
    }

    return true;
}

bool readWAV(const std::string& path, std::vector<float>& samples,
             uint32_t& frameCount, uint32_t& channels, uint32_t& sampleRate) {
    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0);

    ma_decoder decoder;
    ma_result result = ma_decoder_init_file(path.c_str(), &decoderConfig, &decoder);
    if (result != MA_SUCCESS) {
        std::cerr << "[WAV] Failed to open file for reading: " << path << std::endl;
        return false;
    }

    channels = decoder.outputChannels;
    sampleRate = decoder.outputSampleRate;

    // Get total frame count
    ma_uint64 totalFrames;
    result = ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    if (result != MA_SUCCESS) {
        // If length unknown, read in chunks
        totalFrames = 0;
    }

    if (totalFrames > 0) {
        frameCount = static_cast<uint32_t>(totalFrames);
        samples.resize(frameCount * channels);

        ma_uint64 framesRead;
        result = ma_decoder_read_pcm_frames(&decoder, samples.data(), totalFrames, &framesRead);
        frameCount = static_cast<uint32_t>(framesRead);
        samples.resize(frameCount * channels);
    } else {
        // Read in chunks for streams without known length
        samples.clear();
        constexpr uint32_t CHUNK_FRAMES = 4096;
        std::vector<float> chunk(CHUNK_FRAMES * channels);
        frameCount = 0;

        while (true) {
            ma_uint64 framesRead;
            result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), CHUNK_FRAMES, &framesRead);
            if (framesRead == 0) break;

            samples.insert(samples.end(), chunk.begin(),
                          chunk.begin() + framesRead * channels);
            frameCount += static_cast<uint32_t>(framesRead);

            if (framesRead < CHUNK_FRAMES) break;
        }
    }

    ma_decoder_uninit(&decoder);
    return frameCount > 0;
}

} // namespace vivid
