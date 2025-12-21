#pragma once

/**
 * @file audio_output.h
 * @brief AudioOutput operator for speaker playback
 *
 * AudioOutput is the terminal audio operator that sends audio to speakers.
 * It also provides the audio buffer for video export integration.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <memory>
#include <string>
#include <vector>

namespace vivid {

/**
 * @brief Information about an audio output device
 */
struct AudioDeviceInfo {
    std::string name;           ///< Human-readable device name
    std::string id;             ///< Unique device identifier
    uint32_t index;             ///< Device index (for selection)
    bool isDefault;             ///< True if this is the system default device
    uint32_t maxChannels;       ///< Maximum supported channels
    uint32_t minSampleRate;     ///< Minimum supported sample rate
    uint32_t maxSampleRate;     ///< Maximum supported sample rate
};

// Forward declarations
class AudioGraph;

/**
 * @brief Audio output operator for speaker playback
 *
 * AudioOutput receives audio from connected AudioOperators and plays
 * it through the default audio device using miniaudio.
 *
 * @par Example
 * @code
 * chain.add<VideoAudio>("videoAudio").source("video");
 * chain.add<AudioOutput>("audioOut").input("videoAudio").volume(0.8f);
 * chain.audioOutput("audioOut");
 * @endcode
 *
 * When used with video export, the Chain will automatically capture
 * audio from this operator and mux it into the video file.
 */
class AudioOutput : public AudioOperator {
public:
    AudioOutput();
    ~AudioOutput() override;

    // Non-copyable
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // -------------------------------------------------------------------------
    /// @name Device Configuration
    /// @{

    /**
     * @brief Enumerate available audio output devices
     * @return Vector of device info structures
     *
     * Call this before creating AudioOutput to discover available devices.
     * @code
     * auto devices = AudioOutput::enumerateDevices();
     * for (const auto& d : devices) {
     *     std::cout << d.index << ": " << d.name;
     *     if (d.isDefault) std::cout << " (default)";
     *     std::cout << "\n";
     * }
     * @endcode
     */
    static std::vector<AudioDeviceInfo> enumerateDevices();

    /**
     * @brief Set audio device by name
     * @param name Device name (partial match supported)
     *
     * Must be called before init(). If device not found, falls back to default.
     * @code
     * audioOut.setDevice("Focusrite");  // Matches "Focusrite USB Audio"
     * @endcode
     */
    void setDevice(const std::string& name);

    /**
     * @brief Set audio device by index
     * @param index Device index from enumerateDevices()
     *
     * Must be called before init(). Index 0 is typically the default device.
     */
    void setDeviceIndex(uint32_t index);

    /**
     * @brief Set buffer size for latency control
     * @param frames Buffer size in frames (64-2048, default 256)
     *
     * Smaller = lower latency but higher CPU. Must be called before init().
     * - 64 frames = ~1.3ms at 48kHz (very low latency)
     * - 256 frames = ~5.3ms (default, good balance)
     * - 1024 frames = ~21ms (high latency, low CPU)
     */
    void setBufferSize(uint32_t frames);

    /**
     * @brief Get current device name
     */
    std::string deviceName() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Input Configuration
    /// @{

    /**
     * @brief Set input by operator name
     * @param name Name of audio operator to connect
     */
    void setInput(const std::string& name);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "AudioOutput"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    std::vector<ParamDecl> params() override {
        return { m_volumeParam.decl() };
    }

    bool getParam(const std::string& pname, float out[4]) override {
        if (pname == "volume") { out[0] = m_volume; return true; }
        return false;
    }

    bool setParam(const std::string& pname, const float value[4]) override {
        if (pname == "volume") { setVolume(value[0]); return true; }
        return false;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /// @brief Start audio playback
    void play();

    /// @brief Pause audio playback
    void pause();

    /// @brief Check if currently playing
    bool isPlaying() const;

    /// @brief Get current volume
    float getVolume() const { return m_volume; }

    /// @brief Set volume directly
    void setVolume(float v);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Audio Graph Integration
    /// @{

    /**
     * @brief Set the audio graph for pull-based generation
     *
     * In live mode, the miniaudio callback will pull samples directly
     * from this AudioGraph, bypassing the ring buffer.
     *
     * @param graph Pointer to AudioGraph (does not take ownership)
     */
    void setAudioGraph(AudioGraph* graph);

    /**
     * @brief Enable/disable recording mode
     *
     * In recording mode, audio is read from ring buffer instead of
     * being generated in the callback. The video exporter pushes
     * samples to the ring buffer.
     */
    void setRecordingMode(bool recording);

    /**
     * @brief Generate audio for video export (called from main thread)
     *
     * This generates audio synchronously, independent of the callback.
     * Used by video exporter to generate frame-aligned audio.
     *
     * @param output Output buffer (interleaved stereo)
     * @param frameCount Number of frames to generate
     */
    void generateForExport(float* output, uint32_t frameCount);

    /**
     * @brief Push samples to ring buffer for recording mode playback
     */
    void pushToRingBuffer(const float* samples, uint32_t sampleCount);

    /// @}

    // Pull-based audio generation (called from audio thread)
    void generateBlock(uint32_t frameCount) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    std::string m_inputName;
    AudioOperator* m_input = nullptr;  // Resolved input operator
    float m_volume = 1.0f;
    bool m_autoPlay = true;  // Auto-start playback on first audio

    // Device configuration (set before init)
    std::string m_deviceName;           // Device name (empty = default)
    int32_t m_deviceIndex = -1;         // Device index (-1 = use name or default)
    uint32_t m_bufferSize = 256;        // Buffer size in frames

    // Parameter declarations for UI
    Param<float> m_volumeParam{"volume", 1.0f, 0.0f, 2.0f};
};

} // namespace vivid
