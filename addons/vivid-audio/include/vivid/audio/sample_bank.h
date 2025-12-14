#pragma once

/**
 * @file sample_bank.h
 * @brief SampleBank - Load and store multiple audio samples
 */

#include <vivid/operator.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace vivid::audio {

/**
 * @brief A single loaded audio sample
 */
struct Sample {
    std::string name;                   ///< Sample name (filename without extension)
    std::vector<float> samples;         ///< Interleaved stereo float samples
    uint32_t frameCount = 0;            ///< Number of frames
    uint32_t sampleRate = 48000;        ///< Sample rate (always 48kHz after loading)
};

/**
 * @brief Load and store multiple audio samples from a folder
 *
 * SampleBank loads all WAV files from a folder and stores them in memory
 * for instant playback via SamplePlayer.
 *
 * @par Example
 * @code
 * // Load all samples from a folder
 * chain.add<SampleBank>("drums").folder("assets/audio/drums");
 *
 * // Access samples by index or name
 * chain.add<SamplePlayer>("player").bank("drums");
 *
 * // In update:
 * auto& player = chain.get<SamplePlayer>("player");
 * player.trigger(0);              // Trigger by index
 * player.trigger("kick");         // Trigger by name
 * player.trigger(1, 0.8f, 0.5f);  // With volume and pan
 * @endcode
 */
class SampleBank : public Operator {
public:
    SampleBank() = default;
    ~SampleBank() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Load all WAV files from a folder
     * @param path Path to folder containing WAV files
     */
    void setFolder(const std::string& path);

    /**
     * @brief Load a single WAV file
     * @param path Path to WAV file
     */
    void addFile(const std::string& path);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Sample Access
    /// @{

    /**
     * @brief Get number of loaded samples
     */
    size_t count() const { return m_samples.size(); }

    /**
     * @brief Get sample by index
     * @param index Sample index (0 to count()-1)
     * @return Pointer to sample, or nullptr if invalid index
     */
    const Sample* get(size_t index) const;

    /**
     * @brief Get sample by name
     * @param name Sample name (filename without extension)
     * @return Pointer to sample, or nullptr if not found
     */
    const Sample* get(const std::string& name) const;

    /**
     * @brief Get sample index by name
     * @param name Sample name
     * @return Index, or -1 if not found
     */
    int indexOf(const std::string& name) const;

    /**
     * @brief Get all sample names
     */
    std::vector<std::string> names() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "SampleBank"; }

    /// @}

private:
    bool loadWAV(const std::string& path, Sample& outSample);

    std::vector<Sample> m_samples;
    std::unordered_map<std::string, size_t> m_nameIndex;
    std::string m_folderPath;
    std::vector<std::string> m_filePaths;
    bool m_needsLoad = false;
};

} // namespace vivid::audio
