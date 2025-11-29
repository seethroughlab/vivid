#include "miniaudio.h"
#include "audio_capture.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace vivid {

struct AudioCapture::Impl {
    ma_device device;
    ma_context context;
    bool deviceInitialized = false;
    bool contextInitialized = false;
};

AudioCapture::AudioCapture() : impl_(std::make_unique<Impl>()) {}

AudioCapture::~AudioCapture() {
    shutdown();
}

std::vector<AudioDeviceInfo> AudioCapture::listDevices() {
    std::vector<AudioDeviceInfo> devices;

    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        std::cerr << "[AudioCapture] Failed to initialize context for device enumeration\n";
        return devices;
    }

    ma_device_info* captureDevices;
    ma_uint32 captureCount;
    ma_device_info* playbackDevices;
    ma_uint32 playbackCount;

    if (ma_context_get_devices(&context, &playbackDevices, &playbackCount, &captureDevices, &captureCount) != MA_SUCCESS) {
        std::cerr << "[AudioCapture] Failed to enumerate devices\n";
        ma_context_uninit(&context);
        return devices;
    }

    for (ma_uint32 i = 0; i < captureCount; i++) {
        AudioDeviceInfo info;
        info.name = captureDevices[i].name;
        info.index = i;
        info.isDefault = captureDevices[i].isDefault;
        devices.push_back(info);
    }

    ma_context_uninit(&context);
    return devices;
}

bool AudioCapture::init(uint32_t sampleRate, uint32_t channels, int deviceIndex) {
    if (initialized_) {
        shutdown();
    }

    sampleRate_ = sampleRate;
    channels_ = channels;

    // Initialize ring buffer
    bufferSize_ = BUFFER_FRAMES * channels_;
    ringBuffer_.resize(bufferSize_);
    std::fill(ringBuffer_.begin(), ringBuffer_.end(), 0.0f);
    writePos_ = 0;
    readPos_ = 0;
    rmsLevel_ = 0.0f;
    peakLevel_ = 0.0f;

    // Initialize miniaudio context
    if (ma_context_init(nullptr, 0, nullptr, &impl_->context) != MA_SUCCESS) {
        std::cerr << "[AudioCapture] Failed to initialize context\n";
        return false;
    }
    impl_->contextInitialized = true;

    // Configure capture device
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = channels_;
    config.sampleRate = sampleRate_;
    config.dataCallback = &AudioCapture::dataCallback;
    config.pUserData = this;
    config.periodSizeInFrames = 256;  // Low latency

    // Select specific device if requested
    if (deviceIndex >= 0) {
        ma_device_info* captureDevices;
        ma_uint32 captureCount;
        ma_device_info* playbackDevices;
        ma_uint32 playbackCount;

        if (ma_context_get_devices(&impl_->context, &playbackDevices, &playbackCount, &captureDevices, &captureCount) == MA_SUCCESS) {
            if (static_cast<uint32_t>(deviceIndex) < captureCount) {
                config.capture.pDeviceID = &captureDevices[deviceIndex].id;
                std::cout << "[AudioCapture] Using device: " << captureDevices[deviceIndex].name << "\n";
            }
        }
    }

    if (ma_device_init(&impl_->context, &config, &impl_->device) != MA_SUCCESS) {
        std::cerr << "[AudioCapture] Failed to initialize capture device\n";
        ma_context_uninit(&impl_->context);
        impl_->contextInitialized = false;
        return false;
    }

    impl_->deviceInitialized = true;
    initialized_ = true;

    std::cout << "[AudioCapture] Initialized: " << sampleRate_ << "Hz, "
              << channels_ << " channel(s)\n";

    return true;
}

void AudioCapture::shutdown() {
    if (capturing_) {
        stop();
    }

    if (impl_->deviceInitialized) {
        ma_device_uninit(&impl_->device);
        impl_->deviceInitialized = false;
    }

    if (impl_->contextInitialized) {
        ma_context_uninit(&impl_->context);
        impl_->contextInitialized = false;
    }

    initialized_ = false;
}

void AudioCapture::start() {
    if (!initialized_ || capturing_) return;

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        std::cerr << "[AudioCapture] Failed to start capture\n";
        return;
    }
    capturing_ = true;
    std::cout << "[AudioCapture] Started capturing\n";
}

void AudioCapture::stop() {
    if (!initialized_ || !capturing_) return;

    if (ma_device_stop(&impl_->device) != MA_SUCCESS) {
        std::cerr << "[AudioCapture] Failed to stop capture\n";
        return;
    }
    capturing_ = false;
}

uint32_t AudioCapture::getSamples(float* output, uint32_t maxFrames) {
    if (!initialized_ || !output || maxFrames == 0) return 0;

    std::lock_guard<std::mutex> lock(bufferMutex_);

    uint32_t samplesToRead = maxFrames * channels_;
    uint32_t write = writePos_.load();
    uint32_t read = readPos_.load();
    uint32_t available = (write >= read) ? (write - read) : (bufferSize_ - read + write);

    uint32_t toRead = std::min(samplesToRead, available);
    uint32_t framesRead = toRead / channels_;

    for (uint32_t i = 0; i < toRead; i++) {
        output[i] = ringBuffer_[read];
        read = (read + 1) % bufferSize_;
    }

    readPos_ = read;
    return framesRead;
}

uint32_t AudioCapture::peekSamples(float* output, uint32_t frameCount) const {
    if (!initialized_ || !output || frameCount == 0) return 0;

    std::lock_guard<std::mutex> lock(bufferMutex_);

    uint32_t samplesToRead = frameCount * channels_;
    uint32_t write = writePos_.load();
    uint32_t read = readPos_.load();
    uint32_t available = (write >= read) ? (write - read) : (bufferSize_ - read + write);

    uint32_t toRead = std::min(samplesToRead, available);
    uint32_t framesRead = toRead / channels_;

    // Read without modifying readPos_
    uint32_t pos = read;
    for (uint32_t i = 0; i < toRead; i++) {
        output[i] = ringBuffer_[pos];
        pos = (pos + 1) % bufferSize_;
    }

    return framesRead;
}

uint32_t AudioCapture::getBufferedFrames() const {
    uint32_t write = writePos_.load();
    uint32_t read = readPos_.load();
    uint32_t used = (write >= read) ? (write - read) : (bufferSize_ - read + write);
    return used / channels_;
}

void AudioCapture::dataCallback(ma_device* pDevice, void* output, const void* input, ma_uint32 frameCount) {
    (void)output;

    AudioCapture* capture = static_cast<AudioCapture*>(pDevice->pUserData);
    capture->processInput(static_cast<const float*>(input), frameCount);
}

void AudioCapture::processInput(const float* input, uint32_t frameCount) {
    if (!input || frameCount == 0) return;

    float gain = gain_.load();
    uint32_t samplesToWrite = frameCount * channels_;

    // Calculate RMS and peak levels
    float sumSquares = 0.0f;
    float peak = 0.0f;
    for (uint32_t i = 0; i < samplesToWrite; i++) {
        float sample = std::abs(input[i] * gain);
        sumSquares += sample * sample;
        peak = std::max(peak, sample);
    }
    float rms = std::sqrt(sumSquares / samplesToWrite);

    // Smooth level updates
    rmsLevel_ = rmsLevel_.load() * 0.9f + rms * 0.1f;
    peakLevel_ = std::max(peakLevel_.load() * 0.95f, peak);

    // Write to ring buffer
    std::lock_guard<std::mutex> lock(bufferMutex_);

    uint32_t write = writePos_.load();
    uint32_t read = readPos_.load();

    // Calculate available space
    uint32_t used = (write >= read) ? (write - read) : (bufferSize_ - read + write);
    uint32_t available = bufferSize_ - used - 1;

    if (samplesToWrite > available) {
        // Buffer overflow - advance read pointer to make room
        uint32_t overflow = samplesToWrite - available;
        read = (read + overflow) % bufferSize_;
        readPos_ = read;
    }

    // Write samples with gain applied
    for (uint32_t i = 0; i < samplesToWrite; i++) {
        ringBuffer_[write] = input[i] * gain;
        write = (write + 1) % bufferSize_;
    }

    writePos_ = write;
}

} // namespace vivid
