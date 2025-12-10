#pragma once

/**
 * @file audio_operator.h
 * @brief Base class for operators that output audio
 *
 * AudioOperator provides common functionality for operators that
 * produce audio buffers as output, including buffer allocation,
 * input handling, and audio format management.
 */

#include <vivid/operator.h>
#include <vivid/audio_buffer.h>

namespace vivid {

// Forward declaration
class AudioBuffer;

/**
 * @brief Base class for audio-producing operators
 *
 * Provides common functionality for operators that output audio:
 * - Output buffer allocation and management
 * - Input audio buffer access from connected operators
 * - Standard audio format (48kHz stereo)
 *
 * @par Subclassing
 * @code
 * class MyAudioEffect : public AudioOperator {
 * public:
 *     void init(Context& ctx) override {
 *         allocateOutput();  // Create output buffer
 *     }
 *
 *     void process(Context& ctx) override {
 *         const AudioBuffer* in = inputBuffer();
 *         if (in && in->isValid()) {
 *             // Process input to output
 *             for (uint32_t i = 0; i < m_output.sampleCount(); ++i) {
 *                 m_output.samples[i] = processAudio(in->samples[i]);
 *             }
 *         }
 *     }
 *
 *     std::string name() const override { return "MyAudioEffect"; }
 * };
 * @endcode
 */
class AudioOperator : public Operator {
public:
    virtual ~AudioOperator() = default;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    OutputKind outputKind() const override { return OutputKind::Audio; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Output Buffer
    /// @{

    /**
     * @brief Get the output audio buffer
     * @return Pointer to output buffer (may be empty if not allocated)
     */
    virtual const AudioBuffer* outputBuffer() const { return &m_output; }

    /**
     * @brief Get mutable output buffer for writing
     * @return Pointer to output buffer
     */
    AudioBuffer* outputBufferMutable() { return &m_output; }

    /// @brief Get output sample rate
    uint32_t outputSampleRate() const { return m_output.sampleRate; }

    /// @brief Get output channel count
    uint32_t outputChannels() const { return m_output.channels; }

    /// @brief Get output frame count
    uint32_t outputFrameCount() const { return m_output.frameCount; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Input Access
    /// @{

    /**
     * @brief Get input audio buffer from connected operator
     * @param index Input slot index (default 0)
     * @return Audio buffer from input operator, or nullptr if none/incompatible
     *
     * Returns nullptr if:
     * - No operator connected at this index
     * - Connected operator is not an AudioOperator
     * - Connected operator's buffer is not valid
     */
    const AudioBuffer* inputBuffer(int index = 0) const;

    /**
     * @brief Get input as AudioOperator
     * @param index Input slot index (default 0)
     * @return AudioOperator pointer, or nullptr if not an audio operator
     */
    AudioOperator* audioInput(int index = 0) const;

    /// @}

protected:
    // -------------------------------------------------------------------------
    /// @name Buffer Management
    /// @{

    /**
     * @brief Allocate output buffer with standard format
     * @param frames Frame count (default: AUDIO_BLOCK_SIZE = 512)
     * @param channels Channel count (default: AUDIO_CHANNELS = 2)
     * @param sampleRate Sample rate (default: AUDIO_SAMPLE_RATE = 48000)
     */
    void allocateOutput(uint32_t frames = AUDIO_BLOCK_SIZE,
                        uint32_t channels = AUDIO_CHANNELS,
                        uint32_t sampleRate = AUDIO_SAMPLE_RATE);

    /**
     * @brief Clear output buffer to silence
     */
    void clearOutput();

    /**
     * @brief Release output buffer
     */
    void releaseOutput();

    /**
     * @brief Copy input to output (for pass-through or initial buffer)
     * @param index Input index to copy from
     * @return True if copy succeeded, false if no valid input
     */
    bool copyInputToOutput(int index = 0);

    /// @}

    OwnedAudioBuffer m_output;  ///< Output audio buffer
};

} // namespace vivid
