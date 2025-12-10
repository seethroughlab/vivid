/**
 * @file audio_output.cpp
 * @brief AudioOutput operator implementation using miniaudio
 */

// Prevent Windows.h from defining min/max macros
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <vivid/audio_output.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace vivid {

struct AudioOutput::Impl {
    ma_device device;
    bool deviceInitialized = false;

    // Ring buffer for decoupling main thread from audio thread
    std::vector<float> ringBuffer;
    std::atomic<uint32_t> writePos{0};
    std::atomic<uint32_t> readPos{0};
    uint32_t bufferSize = 0;
    std::mutex bufferMutex;

    std::atomic<float> volume{1.0f};
    std::atomic<bool> playing{false};

    static constexpr uint32_t BUFFER_FRAMES = 48000;  // ~1 second at 48kHz

    static void dataCallback(ma_device* pDevice, void* output, const void* input, ma_uint32 frameCount);
    void fillBuffer(float* output, uint32_t frameCount, uint32_t channels);
};

void AudioOutput::Impl::dataCallback(ma_device* pDevice, void* output, const void* input, ma_uint32 frameCount) {
    (void)input;
    Impl* impl = static_cast<Impl*>(pDevice->pUserData);
    impl->fillBuffer(static_cast<float*>(output), frameCount, pDevice->playback.channels);
}

void AudioOutput::Impl::fillBuffer(float* output, uint32_t frameCount, uint32_t channels) {
    uint32_t samplesToRead = frameCount * channels;
    float vol = volume.load();

    std::lock_guard<std::mutex> lock(bufferMutex);

    uint32_t write = writePos.load();
    uint32_t read = readPos.load();
    uint32_t available = (write >= read) ? (write - read) : (bufferSize - read + write);

    uint32_t toRead = std::min(samplesToRead, available);

    // Read samples from ring buffer
    for (uint32_t i = 0; i < toRead; i++) {
        output[i] = ringBuffer[read] * vol;
        read = (read + 1) % bufferSize;
    }

    // Fill remainder with silence if buffer underrun
    if (toRead < samplesToRead) {
        std::memset(output + toRead, 0, (samplesToRead - toRead) * sizeof(float));
    }

    readPos = read;
}

AudioOutput::AudioOutput() : m_impl(std::make_unique<Impl>()) {}

AudioOutput::~AudioOutput() {
    cleanup();
}

AudioOutput& AudioOutput::input(const std::string& name) {
    m_inputName = name;
    return *this;
}

AudioOutput& AudioOutput::volume(float v) {
    m_volume = std::clamp(v, 0.0f, 2.0f);
    if (m_impl) {
        m_impl->volume = m_volume;
    }
    return *this;
}

void AudioOutput::init(Context& ctx) {
    if (m_initialized) {
        return;
    }

    // Resolve input connection by name
    if (!m_inputName.empty()) {
        Operator* op = ctx.chain().getByName(m_inputName);
        if (op && op->outputKind() == OutputKind::Audio) {
            setInput(0, op);
        } else {
            std::cerr << "[AudioOutput] Input '" << m_inputName << "' not found or not an audio operator\n";
        }
    }

    // Initialize ring buffer (stereo)
    m_impl->bufferSize = Impl::BUFFER_FRAMES * AUDIO_CHANNELS;
    m_impl->ringBuffer.resize(m_impl->bufferSize);
    std::fill(m_impl->ringBuffer.begin(), m_impl->ringBuffer.end(), 0.0f);
    m_impl->writePos = 0;
    m_impl->readPos = 0;
    m_impl->volume = m_volume;

    // Configure miniaudio device
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = AUDIO_CHANNELS;
    config.sampleRate = AUDIO_SAMPLE_RATE;
    config.dataCallback = &Impl::dataCallback;
    config.pUserData = m_impl.get();
    config.periodSizeInFrames = AUDIO_BLOCK_SIZE;  // Low latency

    if (ma_device_init(nullptr, &config, &m_impl->device) != MA_SUCCESS) {
        std::cerr << "[AudioOutput] Failed to initialize audio device\n";
        return;
    }

    m_impl->deviceInitialized = true;
    m_initialized = true;

    // Allocate output buffer for export integration
    allocateOutput(AUDIO_BLOCK_SIZE, AUDIO_CHANNELS, AUDIO_SAMPLE_RATE);

    std::cout << "[AudioOutput] Initialized: " << AUDIO_SAMPLE_RATE << "Hz, "
              << AUDIO_CHANNELS << " channels" << std::endl;
}

void AudioOutput::process(Context& ctx) {
    if (!m_initialized) {
        return;
    }

    // Get input audio
    const AudioBuffer* inputBuf = inputBuffer(0);
    if (!inputBuf || !inputBuf->isValid()) {
        // No input - output silence
        clearOutput();
        return;
    }

    // Copy input to output (for video export to capture)
    copyInputToOutput(0);

    // Apply volume to output buffer (for export)
    for (uint32_t i = 0; i < m_output.sampleCount(); ++i) {
        m_output.samples[i] *= m_volume;
    }

    // Push samples to ring buffer for playback
    {
        std::lock_guard<std::mutex> lock(m_impl->bufferMutex);

        uint32_t samplesToWrite = inputBuf->sampleCount();
        uint32_t write = m_impl->writePos.load();
        uint32_t read = m_impl->readPos.load();

        // Calculate available space in ring buffer
        uint32_t used = (write >= read) ? (write - read) : (m_impl->bufferSize - read + write);
        uint32_t available = m_impl->bufferSize - used - 1;

        if (samplesToWrite > available) {
            samplesToWrite = available;
        }

        // Write samples to ring buffer (using output buffer which has volume applied)
        for (uint32_t i = 0; i < samplesToWrite; i++) {
            m_impl->ringBuffer[write] = m_output.samples[i];
            write = (write + 1) % m_impl->bufferSize;
        }

        m_impl->writePos = write;
    }

    // Auto-start playback when we have audio
    if (m_autoPlay && !m_impl->playing && inputBuf->isValid()) {
        play();
    }
}

void AudioOutput::cleanup() {
    if (m_impl->deviceInitialized) {
        ma_device_uninit(&m_impl->device);
        m_impl->deviceInitialized = false;
    }
    m_initialized = false;
    m_impl->playing = false;
    releaseOutput();
}

void AudioOutput::play() {
    if (!m_initialized || m_impl->playing) return;

    if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
        std::cerr << "[AudioOutput] Failed to start playback\n";
        return;
    }
    m_impl->playing = true;
}

void AudioOutput::pause() {
    if (!m_initialized || !m_impl->playing) return;

    if (ma_device_stop(&m_impl->device) != MA_SUCCESS) {
        std::cerr << "[AudioOutput] Failed to stop playback\n";
        return;
    }
    m_impl->playing = false;
}

bool AudioOutput::isPlaying() const {
    return m_impl && m_impl->playing;
}

void AudioOutput::setVolume(float v) {
    m_volume = std::clamp(v, 0.0f, 2.0f);
    if (m_impl) {
        m_impl->volume = m_volume;
    }
}

} // namespace vivid
