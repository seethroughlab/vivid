/**
 * @file audio_in.cpp
 * @brief AudioIn operator implementation using miniaudio
 */

// Prevent Windows.h from defining min/max macros
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vivid/audio/audio_in.h>
#include <vivid/context.h>

// Include miniaudio - it's already implemented in audio_output.cpp,
// so we just need the declarations here
#include "../../core/src/miniaudio.h"

#include <iostream>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace vivid::audio {

struct AudioIn::Impl {
    ma_device device;
    bool deviceInitialized = false;

    // Ring buffer for decoupling audio thread from main thread
    std::vector<float> ringBuffer;
    std::atomic<uint32_t> writePos{0};
    std::atomic<uint32_t> readPos{0};
    uint32_t bufferSize = 0;
    std::mutex bufferMutex;

    std::atomic<float> volume{1.0f};
    std::atomic<bool> capturing{false};

    static constexpr uint32_t BUFFER_FRAMES = 48000;  // ~1 second at 48kHz

    static void dataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    void captureBuffer(const float* input, uint32_t frameCount, uint32_t channels);
};

void AudioIn::Impl::dataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pOutput;  // Not used for capture
    Impl* impl = static_cast<Impl*>(pDevice->pUserData);

    if (pInput && impl->capturing) {
        impl->captureBuffer(static_cast<const float*>(pInput), frameCount, pDevice->capture.channels);
    }
}

void AudioIn::Impl::captureBuffer(const float* input, uint32_t frameCount, uint32_t channels) {
    // Convert to stereo if mono input
    uint32_t samplesToWrite = frameCount * AUDIO_CHANNELS;  // Always output stereo

    std::lock_guard<std::mutex> lock(bufferMutex);

    uint32_t write = writePos.load(std::memory_order_relaxed);
    uint32_t read = readPos.load(std::memory_order_relaxed);

    // Calculate available space in ring buffer
    uint32_t used = (write >= read) ? (write - read) : (bufferSize - read + write);
    uint32_t available = bufferSize - used - 1;

    if (samplesToWrite > available) {
        // Buffer overflow - drop oldest samples by advancing read position
        uint32_t toDrop = samplesToWrite - available;
        read = (read + toDrop) % bufferSize;
        readPos.store(read, std::memory_order_relaxed);
    }

    // Write samples to ring buffer, converting mono to stereo if needed
    float vol = volume.load(std::memory_order_relaxed);

    if (channels == 1) {
        // Mono input - duplicate to stereo
        for (uint32_t i = 0; i < frameCount; i++) {
            float sample = input[i] * vol;
            ringBuffer[write] = sample;  // Left
            write = (write + 1) % bufferSize;
            ringBuffer[write] = sample;  // Right
            write = (write + 1) % bufferSize;
        }
    } else if (channels == 2) {
        // Stereo input - copy directly
        for (uint32_t i = 0; i < frameCount * 2; i++) {
            ringBuffer[write] = input[i] * vol;
            write = (write + 1) % bufferSize;
        }
    } else {
        // Multi-channel - take first two channels
        for (uint32_t i = 0; i < frameCount; i++) {
            ringBuffer[write] = input[i * channels] * vol;      // Left
            write = (write + 1) % bufferSize;
            ringBuffer[write] = input[i * channels + 1] * vol;  // Right
            write = (write + 1) % bufferSize;
        }
    }

    writePos.store(write, std::memory_order_relaxed);
}

AudioIn::AudioIn() : m_impl(std::make_unique<Impl>()) {}

AudioIn::~AudioIn() {
    cleanup();
}

AudioIn& AudioIn::volume(float v) {
    m_volume = std::clamp(v, 0.0f, 2.0f);
    if (m_impl) {
        m_impl->volume = m_volume;
    }
    return *this;
}

AudioIn& AudioIn::mute(bool m) {
    m_muted = m;
    return *this;
}

bool AudioIn::isCapturing() const {
    return m_impl && m_impl->capturing;
}

void AudioIn::init(Context& ctx) {
    if (m_initialized) {
        return;
    }

    // Initialize ring buffer (stereo)
    m_impl->bufferSize = Impl::BUFFER_FRAMES * AUDIO_CHANNELS;
    m_impl->ringBuffer.resize(m_impl->bufferSize);
    std::fill(m_impl->ringBuffer.begin(), m_impl->ringBuffer.end(), 0.0f);
    m_impl->writePos = 0;
    m_impl->readPos = 0;
    m_impl->volume = m_volume;

    // Configure miniaudio device for capture
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = AUDIO_CHANNELS;  // Request stereo, miniaudio will convert if needed
    config.sampleRate = AUDIO_SAMPLE_RATE;
    config.dataCallback = &Impl::dataCallback;
    config.pUserData = m_impl.get();
    config.periodSizeInFrames = AUDIO_BLOCK_SIZE;

    if (ma_device_init(nullptr, &config, &m_impl->device) != MA_SUCCESS) {
        std::cerr << "[AudioIn] Failed to initialize capture device\n";
        return;
    }

    m_impl->deviceInitialized = true;

    // Start capturing immediately
    if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
        std::cerr << "[AudioIn] Failed to start capture\n";
        return;
    }
    m_impl->capturing = true;

    // Allocate output buffer
    allocateOutput(AUDIO_BLOCK_SIZE, AUDIO_CHANNELS, AUDIO_SAMPLE_RATE);

    m_initialized = true;

    std::cout << "[AudioIn] Initialized: " << AUDIO_SAMPLE_RATE << "Hz, "
              << AUDIO_CHANNELS << " channels" << std::endl;
}

void AudioIn::process(Context& ctx) {
    // Capture device is running in background via miniaudio callback
    // Audio generation is handled by generateBlock() on audio thread
}

void AudioIn::generateBlock(uint32_t frameCount) {
    if (!m_initialized) {
        // Output silence
        if (m_output.frameCount != frameCount) {
            m_output.resize(frameCount);
        }
        std::memset(m_output.samples, 0, frameCount * AUDIO_CHANNELS * sizeof(float));
        return;
    }

    // Resize output buffer if needed
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    // If muted, output silence
    if (m_muted) {
        std::memset(m_output.samples, 0, frameCount * AUDIO_CHANNELS * sizeof(float));
        return;
    }

    uint32_t samplesToRead = frameCount * AUDIO_CHANNELS;
    float* out = m_output.samples;

    {
        std::lock_guard<std::mutex> lock(m_impl->bufferMutex);

        uint32_t write = m_impl->writePos.load(std::memory_order_relaxed);
        uint32_t read = m_impl->readPos.load(std::memory_order_relaxed);
        uint32_t available = (write >= read) ? (write - read) : (m_impl->bufferSize - read + write);

        uint32_t toRead = std::min(samplesToRead, available);

        // Read samples from ring buffer
        for (uint32_t i = 0; i < toRead; i++) {
            out[i] = m_impl->ringBuffer[read];
            read = (read + 1) % m_impl->bufferSize;
        }

        // Fill remainder with silence if buffer underrun
        if (toRead < samplesToRead) {
            std::memset(out + toRead, 0, (samplesToRead - toRead) * sizeof(float));
        }

        m_impl->readPos.store(read, std::memory_order_relaxed);
    }
}

void AudioIn::cleanup() {
    if (m_impl->deviceInitialized) {
        m_impl->capturing = false;
        ma_device_uninit(&m_impl->device);
        m_impl->deviceInitialized = false;
    }
    m_initialized = false;
    releaseOutput();
}

} // namespace vivid::audio
