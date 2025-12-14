#pragma once

/**
 * @file sample_player.h
 * @brief SamplePlayer - Play samples with polyphony and pitch control
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/sample_bank.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <array>
#include <atomic>

namespace vivid::audio {

/**
 * @brief Play samples from a SampleBank with polyphony
 *
 * SamplePlayer triggers samples from a connected SampleBank with support
 * for multiple simultaneous voices, pitch/speed control, and volume/pan
 * per trigger.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | volume | float | 0-2 | 1.0 | Master volume |
 * | voices | int | 1-32 | 16 | Maximum polyphony |
 *
 * @par Example
 * @code
 * chain.add<SampleBank>("drums").folder("assets/audio/drums");
 * chain.add<SamplePlayer>("player").bank("drums").voices(8);
 * chain.add<AudioOutput>("out").input("player");
 *
 * // In update():
 * auto& player = chain.get<SamplePlayer>("player");
 *
 * // Trigger by index
 * player.trigger(0);
 *
 * // Trigger by name with volume
 * player.trigger("kick", 0.8f);
 *
 * // Trigger with volume, pan, and pitch
 * player.trigger("snare", 0.9f, 0.2f, 1.0f);  // vol, pan, pitch
 *
 * // Play looped
 * player.triggerLoop(2);
 * player.stop(2);
 * @endcode
 */
class SamplePlayer : public AudioOperator {
public:
    static constexpr int MAX_VOICES = 32;

    Param<float> volume{"volume", 1.0f, 0.0f, 2.0f};  ///< Master volume

    SamplePlayer() {
        registerParam(volume);
    }
    ~SamplePlayer() = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Connect to a SampleBank by name
     * @param bankName Name of the SampleBank operator
     */
    void setBank(const std::string& bankName);

    /**
     * @brief Set maximum polyphony (simultaneous voices)
     * @param v Number of voices (1-32)
     */
    void setVoices(int v);


    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Trigger sample by index
     * @param index Sample index in bank
     */
    void trigger(int index);

    /**
     * @brief Trigger sample by index with volume
     * @param index Sample index
     * @param vol Volume (0-1)
     */
    void trigger(int index, float vol);

    /**
     * @brief Trigger sample by index with volume and pan
     * @param index Sample index
     * @param vol Volume (0-1)
     * @param pan Pan (-1 left, 0 center, 1 right)
     */
    void trigger(int index, float vol, float pan);

    /**
     * @brief Trigger sample with full control
     * @param index Sample index
     * @param vol Volume (0-1)
     * @param pan Pan (-1 to 1)
     * @param pitch Pitch multiplier (0.5 = octave down, 2.0 = octave up)
     */
    void trigger(int index, float vol, float pan, float pitch);

    /**
     * @brief Trigger sample by name
     */
    void trigger(const std::string& name);
    void trigger(const std::string& name, float vol);
    void trigger(const std::string& name, float vol, float pan);
    void trigger(const std::string& name, float vol, float pan, float pitch);

    /**
     * @brief Trigger sample looped
     * @param index Sample index
     * @return Voice ID for stopping
     */
    int triggerLoop(int index);
    int triggerLoop(int index, float vol, float pan = 0.0f, float pitch = 1.0f);
    int triggerLoop(const std::string& name);
    int triggerLoop(const std::string& name, float vol, float pan = 0.0f, float pitch = 1.0f);

    /**
     * @brief Stop a specific voice
     * @param voiceId Voice ID returned from triggerLoop
     */
    void stop(int voiceId);

    /**
     * @brief Stop all voices playing a specific sample
     * @param index Sample index to stop
     */
    void stopSample(int index);
    void stopSample(const std::string& name);

    /**
     * @brief Stop all playing voices
     */
    void stopAll();

    /**
     * @brief Check if a voice is playing
     */
    bool isPlaying(int voiceId) const;

    /**
     * @brief Get number of active voices
     */
    int activeVoices() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "SamplePlayer"; }

    void generateBlock(uint32_t frameCount) override;

    /// @}

private:
    struct Voice {
        int sampleIndex = -1;
        double position = 0.0;    // Fractional position for pitch shifting
        float volume = 1.0f;
        float panL = 1.0f;
        float panR = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool active = false;
    };

    int findFreeVoice();
    int triggerInternal(int sampleIndex, float vol, float pan, float pitch, bool loop);

    std::string m_bankName;
    SampleBank* m_bank = nullptr;
    int m_maxVoices = 16;

    std::array<Voice, MAX_VOICES> m_voices;

    bool m_initialized = false;
};

} // namespace vivid::audio
