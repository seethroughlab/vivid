/**
 * @file audio_output.cpp
 * @brief AudioOutput operator implementation using miniaudio
 *
 * This is the bridge between the AudioGraph and the audio hardware.
 * In live mode, the miniaudio callback pulls samples directly from AudioGraph.
 * In recording mode, Chain::process() generates audio synchronously.
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
#include <vivid/audio_graph.h>
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

    // Pull-based audio generation
    AudioGraph* audioGraph = nullptr;

    // Ring buffer for recording mode (video export) - NOT used for live playback
    std::vector<float> ringBuffer;
    std::atomic<uint32_t> writePos{0};
    std::atomic<uint32_t> readPos{0};
    uint32_t bufferSize = 0;

    std::atomic<float> volume{1.0f};
    std::atomic<bool> playing{false};
    std::atomic<bool> recordingMode{false};  // True when exporting video

    static constexpr uint32_t BUFFER_FRAMES = 48000;  // ~1 second at 48kHz

    static void dataCallback(ma_device* pDevice, void* output, const void* input, ma_uint32 frameCount);
};

void AudioOutput::Impl::dataCallback(ma_device* pDevice, void* output, const void* input, ma_uint32 frameCount) {
    (void)input;
    Impl* impl = static_cast<Impl*>(pDevice->pUserData);
    float* out = static_cast<float*>(output);
    uint32_t channels = pDevice->playback.channels;

    if (impl->recordingMode.load(std::memory_order_relaxed)) {
        // Recording mode: read from ring buffer (filled by main thread)
        uint32_t samplesToRead = frameCount * channels;
        uint32_t write = impl->writePos.load(std::memory_order_acquire);
        uint32_t read = impl->readPos.load(std::memory_order_relaxed);
        uint32_t available = (write >= read) ? (write - read) : (impl->bufferSize - read + write);

        uint32_t toRead = std::min(samplesToRead, available);

        for (uint32_t i = 0; i < toRead; i++) {
            out[i] = impl->ringBuffer[read];
            read = (read + 1) % impl->bufferSize;
        }

        if (toRead < samplesToRead) {
            std::memset(out + toRead, 0, (samplesToRead - toRead) * sizeof(float));
        }

        impl->readPos.store(read, std::memory_order_release);
    } else {
        // Live mode: pull directly from AudioGraph (the key change!)
        if (impl->audioGraph) {
            impl->audioGraph->processBlock(out, frameCount);

            // Apply volume
            float vol = impl->volume.load(std::memory_order_relaxed);
            for (uint32_t i = 0; i < frameCount * channels; ++i) {
                out[i] *= vol;
            }
        } else {
            // No audio graph - silence
            std::memset(out, 0, frameCount * channels * sizeof(float));
        }
    }
}

AudioOutput::AudioOutput() : m_impl(std::make_unique<Impl>()) {}

AudioOutput::~AudioOutput() {
    cleanup();
}

void AudioOutput::setInput(const std::string& name) {
    m_inputName = name;
}

void AudioOutput::setAudioGraph(AudioGraph* graph) {
    m_impl->audioGraph = graph;
}

void AudioOutput::setRecordingMode(bool recording) {
    m_impl->recordingMode.store(recording, std::memory_order_release);

    if (recording) {
        // Reset ring buffer for recording
        m_impl->writePos.store(0, std::memory_order_relaxed);
        m_impl->readPos.store(0, std::memory_order_relaxed);
        std::fill(m_impl->ringBuffer.begin(), m_impl->ringBuffer.end(), 0.0f);
    }
}

void AudioOutput::init(Context& ctx) {
    if (m_initialized) {
        return;
    }

    // Resolve input operator by name
    if (!m_inputName.empty()) {
        Operator* op = ctx.chain().getByName(m_inputName);
        m_input = dynamic_cast<AudioOperator*>(op);
        if (m_input) {
            Operator::setInput(0, m_input);  // Register dependency
        }
    }

    // Initialize ring buffer for recording mode (stereo)
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
    config.periodSizeInFrames = 256;  // Low latency (~5ms at 48kHz)

    if (ma_device_init(nullptr, &config, &m_impl->device) != MA_SUCCESS) {
        std::cerr << "[AudioOutput] Failed to initialize audio device\n";
        return;
    }

    m_impl->deviceInitialized = true;
    m_initialized = true;

    // Allocate output buffer for export integration
    allocateOutput(AUDIO_BLOCK_SIZE, AUDIO_CHANNELS, AUDIO_SAMPLE_RATE);

    std::cout << "[AudioOutput] Initialized: " << AUDIO_SAMPLE_RATE << "Hz, "
              << AUDIO_CHANNELS << " channels (pull-based)" << std::endl;
}

void AudioOutput::generateBlock(uint32_t frameCount) {
    if (!m_initialized) return;

    // Resize output buffer if needed
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    // Copy from input operator's output buffer
    if (m_input) {
        const AudioBuffer* inBuf = m_input->outputBuffer();
        if (inBuf && inBuf->isValid()) {
            uint32_t sampleCount = std::min(frameCount * AUDIO_CHANNELS, inBuf->sampleCount());
            std::memcpy(m_output.samples, inBuf->samples, sampleCount * sizeof(float));

            // Zero any remaining samples
            if (sampleCount < frameCount * AUDIO_CHANNELS) {
                std::memset(m_output.samples + sampleCount, 0,
                           (frameCount * AUDIO_CHANNELS - sampleCount) * sizeof(float));
            }
        } else {
            // No valid input - silence
            std::memset(m_output.samples, 0, frameCount * AUDIO_CHANNELS * sizeof(float));
        }
    } else {
        // No input connected - silence
        std::memset(m_output.samples, 0, frameCount * AUDIO_CHANNELS * sizeof(float));
    }
}

void AudioOutput::process(Context& ctx) {
    if (!m_initialized) {
        return;
    }

    // In live (non-recording) mode, audio is generated by the audio callback.
    // process() is a no-op for live playback.

    // Auto-start playback
    if (m_autoPlay && !m_impl->playing) {
        play();
    }

    // For recording mode, we would push to ring buffer here.
    // But that's handled by the video exporter calling generateForExport().
}

void AudioOutput::generateForExport(float* output, uint32_t frameCount) {
    // Called by video exporter to generate audio synchronously
    if (m_impl->audioGraph) {
        m_impl->audioGraph->processBlock(output, frameCount);

        // Apply volume
        for (uint32_t i = 0; i < frameCount * AUDIO_CHANNELS; ++i) {
            output[i] *= m_volume;
        }
    } else {
        std::memset(output, 0, frameCount * AUDIO_CHANNELS * sizeof(float));
    }
}

void AudioOutput::pushToRingBuffer(const float* samples, uint32_t sampleCount) {
    // Used by recording mode to push samples to ring buffer for playback
    uint32_t write = m_impl->writePos.load(std::memory_order_relaxed);
    uint32_t read = m_impl->readPos.load(std::memory_order_acquire);

    uint32_t used = (write >= read) ? (write - read) : (m_impl->bufferSize - read + write);
    uint32_t available = m_impl->bufferSize - used - 1;

    uint32_t toWrite = std::min(sampleCount, available);

    for (uint32_t i = 0; i < toWrite; i++) {
        m_impl->ringBuffer[write] = samples[i];
        write = (write + 1) % m_impl->bufferSize;
    }

    m_impl->writePos.store(write, std::memory_order_release);
}

void AudioOutput::cleanup() {
    if (m_impl->deviceInitialized) {
        ma_device_uninit(&m_impl->device);
        m_impl->deviceInitialized = false;
    }
    m_initialized = false;
    m_impl->playing = false;
    m_impl->audioGraph = nullptr;
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
