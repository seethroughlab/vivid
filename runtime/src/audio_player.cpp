// Prevent Windows.h from defining min/max macros (must be before miniaudio.h)
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "audio_player.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace vivid {

struct AudioPlayer::Impl {
    ma_device device;
    bool deviceInitialized = false;
};

AudioPlayer::AudioPlayer() : impl_(std::make_unique<Impl>()) {}

AudioPlayer::~AudioPlayer() {
    shutdown();
}

bool AudioPlayer::init(uint32_t sampleRate, uint32_t channels) {
    if (initialized_) {
        shutdown();
    }

    sampleRate_ = sampleRate;
    channels_ = channels;

    // Initialize ring buffer (stereo interleaved)
    bufferSize_ = BUFFER_FRAMES * channels_;
    ringBuffer_.resize(bufferSize_);
    std::fill(ringBuffer_.begin(), ringBuffer_.end(), 0.0f);
    writePos_ = 0;
    readPos_ = 0;
    samplesPlayed_ = 0;

    // Configure miniaudio device
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = channels_;
    config.sampleRate = sampleRate_;
    config.dataCallback = &AudioPlayer::dataCallback;
    config.pUserData = this;
    config.periodSizeInFrames = 512;  // Low latency

    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
        std::cerr << "[AudioPlayer] Failed to initialize audio device\n";
        return false;
    }

    impl_->deviceInitialized = true;
    initialized_ = true;

    std::cout << "[AudioPlayer] Initialized: " << sampleRate_ << "Hz, "
              << channels_ << " channel(s)\n";

    return true;
}

void AudioPlayer::shutdown() {
    if (impl_->deviceInitialized) {
        ma_device_uninit(&impl_->device);
        impl_->deviceInitialized = false;
    }
    initialized_ = false;
    playing_ = false;
}

void AudioPlayer::play() {
    if (!initialized_ || playing_) return;

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        std::cerr << "[AudioPlayer] Failed to start playback\n";
        return;
    }
    playing_ = true;
}

void AudioPlayer::pause() {
    if (!initialized_ || !playing_) return;

    if (ma_device_stop(&impl_->device) != MA_SUCCESS) {
        std::cerr << "[AudioPlayer] Failed to stop playback\n";
        return;
    }
    playing_ = false;
}

void AudioPlayer::flush() {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    std::fill(ringBuffer_.begin(), ringBuffer_.end(), 0.0f);
    writePos_ = 0;
    readPos_ = 0;
    samplesPlayed_ = 0;
}

void AudioPlayer::pushSamples(const float* samples, uint32_t frameCount) {
    if (!initialized_ || !samples || frameCount == 0) return;

    std::lock_guard<std::mutex> lock(bufferMutex_);

    uint32_t samplesToWrite = frameCount * channels_;
    uint32_t write = writePos_.load();
    uint32_t read = readPos_.load();

    // Calculate available space in ring buffer
    uint32_t used = (write >= read) ? (write - read) : (bufferSize_ - read + write);
    uint32_t available = bufferSize_ - used - 1;  // -1 to distinguish full from empty

    if (samplesToWrite > available) {
        // Buffer full - drop oldest samples or skip newest
        // For now, just write what we can
        samplesToWrite = available;
    }

    // Write samples to ring buffer
    for (uint32_t i = 0; i < samplesToWrite; i++) {
        ringBuffer_[write] = samples[i];
        write = (write + 1) % bufferSize_;
    }

    writePos_ = write;
}

double AudioPlayer::getPlaybackPosition() const {
    if (sampleRate_ == 0) return 0.0;
    return static_cast<double>(samplesPlayed_.load()) / static_cast<double>(sampleRate_);
}

uint32_t AudioPlayer::getBufferedFrames() const {
    uint32_t write = writePos_.load();
    uint32_t read = readPos_.load();
    uint32_t used = (write >= read) ? (write - read) : (bufferSize_ - read + write);
    return used / channels_;
}

void AudioPlayer::setVolume(float volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);
}

void AudioPlayer::dataCallback(ma_device* pDevice, void* output, const void* input, ma_uint32 frameCount) {
    (void)input;

    AudioPlayer* player = static_cast<AudioPlayer*>(pDevice->pUserData);
    player->fillBuffer(static_cast<float*>(output), frameCount);
}

void AudioPlayer::fillBuffer(float* output, uint32_t frameCount) {
    uint32_t samplesToRead = frameCount * channels_;
    float volume = volume_.load();

    std::lock_guard<std::mutex> lock(bufferMutex_);

    uint32_t write = writePos_.load();
    uint32_t read = readPos_.load();
    uint32_t available = (write >= read) ? (write - read) : (bufferSize_ - read + write);

    uint32_t toRead = std::min(samplesToRead, available);

    // Read samples from ring buffer
    for (uint32_t i = 0; i < toRead; i++) {
        output[i] = ringBuffer_[read] * volume;
        read = (read + 1) % bufferSize_;
    }

    // Fill remainder with silence if buffer underrun
    if (toRead < samplesToRead) {
        std::memset(output + toRead, 0, (samplesToRead - toRead) * sizeof(float));
    }

    readPos_ = read;
    samplesPlayed_ += toRead / channels_;
}

} // namespace vivid
