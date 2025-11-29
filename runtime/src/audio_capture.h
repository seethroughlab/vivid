#pragma once
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>
#include <string>
#include <cstdint>

namespace vivid {

/**
 * @brief Audio capture device information.
 */
struct AudioDeviceInfo {
    std::string name;
    uint32_t index;
    bool isDefault;
};

/**
 * @brief Audio capture using miniaudio.
 *
 * Captures audio from microphone or line-in and provides samples
 * via a ring buffer for processing by operators.
 */
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    // Non-copyable
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    /**
     * @brief List available audio input devices.
     * @return Vector of device information.
     */
    static std::vector<AudioDeviceInfo> listDevices();

    /**
     * @brief Initialize audio capture.
     * @param sampleRate Sample rate in Hz (e.g., 44100, 48000).
     * @param channels Number of channels (1 for mono, 2 for stereo).
     * @param deviceIndex Device index (-1 for default).
     * @return true if initialization succeeded.
     */
    bool init(uint32_t sampleRate = 44100, uint32_t channels = 1, int deviceIndex = -1);

    /**
     * @brief Shutdown audio capture and release resources.
     */
    void shutdown();

    /**
     * @brief Check if audio capture is initialized.
     */
    bool isInitialized() const { return initialized_; }

    /**
     * @brief Start audio capture.
     */
    void start();

    /**
     * @brief Stop audio capture.
     */
    void stop();

    /**
     * @brief Check if currently capturing.
     */
    bool isCapturing() const { return capturing_; }

    /**
     * @brief Get available samples from capture buffer.
     * @param output Buffer to receive samples (interleaved float -1.0 to 1.0).
     * @param maxFrames Maximum number of frames to read.
     * @return Number of frames actually read.
     */
    uint32_t getSamples(float* output, uint32_t maxFrames);

    /**
     * @brief Get a copy of recent samples without consuming them.
     * Useful for FFT analysis while keeping the buffer intact.
     * @param output Buffer to receive samples.
     * @param frameCount Number of frames to peek (will be clamped to available).
     * @return Number of frames actually copied.
     */
    uint32_t peekSamples(float* output, uint32_t frameCount) const;

    /**
     * @brief Get the number of buffered frames available.
     */
    uint32_t getBufferedFrames() const;

    /**
     * @brief Get current RMS level (0.0 to 1.0).
     * Updated continuously during capture.
     */
    float getRMSLevel() const { return rmsLevel_.load(); }

    /**
     * @brief Get current peak level (0.0 to 1.0).
     * Updated continuously during capture.
     */
    float getPeakLevel() const { return peakLevel_.load(); }

    /**
     * @brief Get sample rate.
     */
    uint32_t getSampleRate() const { return sampleRate_; }

    /**
     * @brief Get number of channels.
     */
    uint32_t getChannels() const { return channels_; }

    /**
     * @brief Set input gain.
     * @param gain Gain multiplier (default 1.0).
     */
    void setGain(float gain) { gain_ = gain; }

    /**
     * @brief Get current input gain.
     */
    float getGain() const { return gain_.load(); }

private:
    // Called by miniaudio when audio data is available
    static void dataCallback(struct ma_device* device, void* output, const void* input, unsigned int frameCount);
    void processInput(const float* input, uint32_t frameCount);

    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Ring buffer for captured samples
    std::vector<float> ringBuffer_;
    std::atomic<uint32_t> writePos_{0};
    std::atomic<uint32_t> readPos_{0};
    uint32_t bufferSize_ = 0;
    mutable std::mutex bufferMutex_;

    uint32_t sampleRate_ = 44100;
    uint32_t channels_ = 1;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> capturing_{false};
    std::atomic<float> gain_{1.0f};

    // Level metering
    std::atomic<float> rmsLevel_{0.0f};
    std::atomic<float> peakLevel_{0.0f};

    static constexpr uint32_t BUFFER_FRAMES = 8192;  // ~185ms at 44.1kHz
};

} // namespace vivid
