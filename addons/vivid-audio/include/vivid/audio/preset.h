#pragma once

/**
 * @file preset.h
 * @brief Preset save/load system for synths
 *
 * Provides a mixin class for synths to save/load their parameters as JSON presets.
 * Supports both factory presets (shipped with app) and user presets (~/.vivid/presets/).
 */

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <filesystem>

namespace vivid::audio {

/**
 * @brief Preset metadata
 */
struct PresetInfo {
    std::string name;           ///< Display name
    std::string path;           ///< Full path to preset file
    std::string author;         ///< Author name
    std::string category;       ///< Category (e.g., "Pads", "Bass")
    bool isFactory = false;     ///< True if factory preset (read-only)
};

/**
 * @brief Mixin class for preset-capable operators
 *
 * Synths that inherit from this can save/load their parameters as JSON files.
 * The preset format includes all Param<T> values plus any extra state
 * (like algorithm selection) via serializeExtra/deserializeExtra hooks.
 *
 * @par Example
 * @code
 * class FMSynth : public AudioOperator, public PresetCapable {
 * public:
 *     bool savePreset(const std::string& path) override;
 *     bool loadPresetFile(const std::string& path) override;
 * protected:
 *     std::string synthType() const override { return "FMSynth"; }
 *     void serializeExtra(nlohmann::json& j) const override {
 *         j["algorithm"] = algorithmName(m_algorithm);
 *     }
 * };
 * @endcode
 */
class PresetCapable {
public:
    virtual ~PresetCapable() = default;

    /**
     * @brief Save current state to a preset file
     * @param path Path to save (should end in .json)
     * @param name Display name for the preset
     * @param author Author name (optional)
     * @param category Category string (optional)
     * @return true if saved successfully
     */
    virtual bool savePreset(const std::string& path,
                           const std::string& name = "",
                           const std::string& author = "",
                           const std::string& category = "") = 0;

    /**
     * @brief Load a preset from file
     * @param path Path to preset file
     * @return true if loaded successfully
     */
    virtual bool loadPresetFile(const std::string& path) = 0;

    /**
     * @brief List available presets for a synth type
     * @param synthType Type identifier (e.g., "FMSynth")
     * @return Vector of preset info
     */
    static std::vector<PresetInfo> listPresets(const std::string& synthType);

    /**
     * @brief Get user preset directory
     * @return Path to ~/.vivid/presets/
     */
    static std::filesystem::path userPresetDir();

    /**
     * @brief Get factory preset directory (relative to executable)
     * @return Path to presets/ next to executable
     */
    static std::filesystem::path factoryPresetDir();

protected:
    /**
     * @brief Get synth type identifier for preset format
     * @return Type string (e.g., "FMSynth", "PolySynth")
     */
    virtual std::string synthType() const = 0;

    /**
     * @brief Serialize extra state beyond Param values
     * @param j JSON object to add fields to
     *
     * Override to save algorithm, waveform selection, etc.
     */
    virtual void serializeExtra(nlohmann::json& j) const {}

    /**
     * @brief Deserialize extra state
     * @param j JSON object to read from
     *
     * Override to restore algorithm, waveform selection, etc.
     */
    virtual void deserializeExtra(const nlohmann::json& j) {}
};

} // namespace vivid::audio
