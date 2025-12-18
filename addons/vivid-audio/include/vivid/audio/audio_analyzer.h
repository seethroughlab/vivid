#pragma once

/**
 * @file audio_analyzer.h
 * @brief Base class for audio analysis operators
 *
 * AudioAnalyzer provides common functionality for audio analysis:
 * - Named input connection to audio operators
 * - Outputs AudioValue (analysis results, not audio)
 *
 * Subclasses provide getters for analysis values (rms, peak, spectrum, etc.)
 */

#include <vivid/operator.h>
#include <vivid/audio_operator.h>
#include <vivid/param_registry.h>
#include <string>

namespace vivid::audio {

/**
 * @brief Base class for audio analysis operators
 *
 * Analysis operators take audio input and extract values like:
 * - Amplitude levels (RMS, peak)
 * - Frequency spectrum (FFT)
 * - Frequency bands (bass, mids, highs)
 * - Beat/onset detection
 *
 * These values can be read in update() to drive visual parameters.
 *
 * @par Example
 * @code
 * chain.add<Levels>("levels").input("audio");
 * chain.add<FFT>("fft").input("audio").size(1024);
 *
 * // In update():
 * float volume = chain.get<Levels>("levels").rms();
 * float bass = chain.get<FFT>("fft").band(20, 250);
 * @endcode
 */
class AudioAnalyzer : public Operator, public ParamRegistry {
public:
    virtual ~AudioAnalyzer() = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Connect to audio source by name
     * @param name Name of the source audio operator
     */
    void input(const std::string& name) {
        m_inputName = name;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    OutputKind outputKind() const override { return OutputKind::AudioValue; }

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Parameter Interface
    /// Delegates to ParamRegistry for unified param access
    /// @{

    std::vector<ParamDecl> params() override { return registeredParams(); }
    bool getParam(const std::string& name, float out[4]) override { return getRegisteredParam(name, out); }
    bool setParam(const std::string& name, const float value[4]) override { return setRegisteredParam(name, value); }

    /// @}

protected:
    /**
     * @brief Initialize analyzer-specific state
     * Override in subclass to allocate buffers, etc.
     */
    virtual void initAnalyzer(Context& ctx) {}

    /**
     * @brief Analyze the audio buffer
     * @param input Input audio samples (interleaved stereo)
     * @param frames Number of frames to analyze
     *
     * Override in subclass to compute analysis values.
     */
    virtual void analyze(const float* input, uint32_t frames, uint32_t channels) = 0;

    /**
     * @brief Clean up analyzer-specific resources
     */
    virtual void cleanupAnalyzer() {}

    /**
     * @brief Get the connected audio input
     */
    const AudioBuffer* getInputBuffer() const;

    // Input connection
    std::string m_inputName;
    AudioOperator* m_connectedInput = nullptr;
};

} // namespace vivid::audio
